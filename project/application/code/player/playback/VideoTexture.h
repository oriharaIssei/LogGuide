#pragma once

/// api
#include <d3d12.h>

/// stl
#include <cstdint>
#include <memory>

/// engine
#include "directX12/DxDescriptor.h"
#include "directX12/DxFence.h"
#include "directX12/DxResource.h"

namespace OriGine {
class DxCommand;
}

namespace LogGuide {

// =============================================================================
// VideoTexture
//
// トップダウン BGRA(RGB32) の CPU フレームを毎フレーム GPU テクスチャへ転送し、
// ImGui::Image で表示するための SRV GPU ハンドルを提供する動的テクスチャ。
//
// SRV はエンジンのグローバル SRV ヒープ(Engine::GetSrvHeap())から確保する。
// このヒープは ImGuiManager が描画時に SetDescriptorHeaps でバインドするものと
// 同一なので、GetGpuHandle() を ImGui::Image にそのまま渡せる。
//
// 転送は専用の DxCommand + 専用 DxFence で行い、エンジン本体のフレーム同期とは
// 独立させている(コピー完了を待ってから ImGui 描画に入る)。
// =============================================================================
class VideoTexture {
public:
    VideoTexture();
    ~VideoTexture();

    VideoTexture(const VideoTexture&)            = delete;
    VideoTexture& operator=(const VideoTexture&) = delete;

    void Initialize();
    void Finalize();

    // bgra: width*height*4 バイトのトップダウン BGRA。サイズが変われば内部で作り直す。
    void Update(const uint8_t* bgra, uint32_t width, uint32_t height);

    bool     IsValid() const { return valid_; }
    uint32_t GetWidth() const { return width_; }
    uint32_t GetHeight() const { return height_; }

    // ImGui::Image に渡す GPU ディスクリプタハンドル(.ptr を ImTextureID にキャストして使う)。
    D3D12_GPU_DESCRIPTOR_HANDLE GetGpuHandle() const { return srv_.GetGpuHandle(); }

private:
    void Recreate(uint32_t width, uint32_t height);
    void ReleaseGpuResources();

    std::unique_ptr<OriGine::DxCommand> command_;
    OriGine::DxFence                    fence_;

    OriGine::DxResource     texture_;
    OriGine::DxResource     upload_;
    OriGine::DxSrvDescriptor srv_;
    bool                    hasSrv_ = false;

    uint32_t              width_  = 0;
    uint32_t              height_ = 0;
    D3D12_RESOURCE_STATES state_  = D3D12_RESOURCE_STATE_COPY_DEST;
    bool                  valid_  = false;
};

} // namespace LogGuide
