#include "player/VideoTexture.h"

/// engine
#include "Engine.h"
#include "directX12/DxCommand.h"
#include "directX12/DxDevice.h"

/// directXTex (UpdateSubresources / GetRequiredIntermediateSize)
#include <DirectXTex/d3dx12.h>

/// stl
#include <atomic>
#include <string>

namespace LogGuide {

namespace {
// VideoTexture ごとに一意なコマンドリストキーを割り当てる(共有による競合を避ける)。
std::string NextCommandKey() {
    static std::atomic<uint32_t> counter{0};
    return "logguide.videoTex." + std::to_string(counter.fetch_add(1));
}
} // namespace

VideoTexture::VideoTexture()  = default;
VideoTexture::~VideoTexture() {
    Finalize();
}

void VideoTexture::Initialize() {
    const std::string key = NextCommandKey();
    command_ = std::make_unique<OriGine::DxCommand>();
    command_->Initialize(key, key);
    fence_.Initialize(OriGine::Engine::GetInstance()->GetDxDevice()->device_);
}

void VideoTexture::Finalize() {
    ReleaseGpuResources();
    if (command_) {
        command_->Finalize();
        command_.reset();
    }
    fence_.Finalize();
}

void VideoTexture::ReleaseGpuResources() {
    if (hasSrv_) {
        OriGine::Engine::GetInstance()->GetSrvHeap()->ReleaseDescriptor(srv_);
        hasSrv_ = false;
    }
    if (texture_.IsValid()) {
        texture_.Finalize();
    }
    if (upload_.IsValid()) {
        upload_.Finalize();
    }
    valid_  = false;
    width_  = 0;
    height_ = 0;
}

void VideoTexture::Recreate(uint32_t width, uint32_t height) {
    ReleaseGpuResources();

    auto device = OriGine::Engine::GetInstance()->GetDxDevice()->device_;

    // 2D / BGRA8 / mip1 のテクスチャメタデータを作る。
    DirectX::TexMetadata meta{};
    meta.width     = width;
    meta.height    = height;
    meta.depth     = 1;
    meta.arraySize = 1;
    meta.mipLevels = 1;
    meta.format    = DXGI_FORMAT_B8G8R8A8_UNORM;
    meta.dimension = DirectX::TEX_DIMENSION_TEXTURE2D;

    texture_.CreateTextureResource(device, meta); // 生成直後は COPY_DEST
    state_ = D3D12_RESOURCE_STATE_COPY_DEST;

    const uint64_t uploadSize = GetRequiredIntermediateSize(texture_.GetResource().Get(), 0, 1);
    upload_.CreateBufferResource(device, static_cast<size_t>(uploadSize)); // UPLOAD ヒープ

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    // Mp4Player は映像を RGB32(= BGRX, 上位 8bit は未使用)でデコードするため、
    // アルファバイトが 0 のまま来る。B8G8R8A8_UNORM としてそのまま SRV 化すると
    // アルファ=0 になり、ImGui::Image のアルファブレンドで映像が完全に透明になって
    // しまう(背景が透けて「灰色の空枠」に見える)。SRV のスウィズルでアルファを
    // 強制的に 1(不透明)にして回避する。
    srvDesc.Shader4ComponentMapping = D3D12_ENCODE_SHADER_4_COMPONENT_MAPPING(
        D3D12_SHADER_COMPONENT_MAPPING_FROM_MEMORY_COMPONENT_0,
        D3D12_SHADER_COMPONENT_MAPPING_FROM_MEMORY_COMPONENT_1,
        D3D12_SHADER_COMPONENT_MAPPING_FROM_MEMORY_COMPONENT_2,
        D3D12_SHADER_COMPONENT_MAPPING_FORCE_VALUE_1);
    srvDesc.ViewDimension       = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;

    OriGine::SRVEntry entry(&texture_, srvDesc);
    srv_    = OriGine::Engine::GetInstance()->GetSrvHeap()->CreateDescriptor(&entry);
    hasSrv_ = true;

    width_  = width;
    height_ = height;
    valid_  = true;
}

void VideoTexture::Update(const uint8_t* bgra, uint32_t width, uint32_t height) {
    if (!bgra || width == 0 || height == 0) {
        return;
    }
    if (!valid_ || width != width_ || height != height_) {
        Recreate(width, height);
    }

    auto* list = command_->GetCommandList().Get();

    auto transition = [&](D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource   = texture_.GetResource().Get();
        barrier.Transition.StateBefore = before;
        barrier.Transition.StateAfter  = after;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        list->ResourceBarrier(1, &barrier);
    };

    if (state_ != D3D12_RESOURCE_STATE_COPY_DEST) {
        transition(state_, D3D12_RESOURCE_STATE_COPY_DEST);
        state_ = D3D12_RESOURCE_STATE_COPY_DEST;
    }

    D3D12_SUBRESOURCE_DATA sub{};
    sub.pData      = bgra;
    sub.RowPitch   = static_cast<LONG_PTR>(width) * 4;
    sub.SlicePitch = static_cast<LONG_PTR>(width) * 4 * height;

    UpdateSubresources(list, texture_.GetResource().Get(), upload_.GetResource().Get(), 0, 0, 1, &sub);

    // このコピーは VideoTexture 専用キューで実行するが、実際に SRV として読むのは
    // エンジンの "main" グラフィクスキュー(ImGui 描画)である。
    // D3D12 ではリソースをキュー間で受け渡すとき COMMON 状態を経由しないと未定義動作
    // (多くのドライバで黒画像になる)。そこで受け渡しは COMMON で行い、main キュー側では
    // SRV サンプリング時の暗黙昇格(COMMON -> PIXEL_SHADER_RESOURCE)に任せる。
    transition(D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON);
    state_ = D3D12_RESOURCE_STATE_COMMON;

    command_->Close();
    command_->ExecuteCommand();

    const UINT64 signalValue = fence_.Signal(command_->GetCommandQueue());
    fence_.WaitForFence(signalValue);

    command_->CommandReset();
}

} // namespace LogGuide
