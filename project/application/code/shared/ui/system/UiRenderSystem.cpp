#include "ui/system/UiRenderSystem.h"

/// engine
#include "Engine.h"
#include "directX12/DxCommand.h"
// Engine.h は DxDevice を前方宣言しているだけなので、device_ を触るには定義が要る
#include "directX12/DxDevice.h"
#include "directX12/ShaderManager.h"
#include "text/BitmapFont.h"
#include "text/FontManager.h"
#include "winApp/WinApp.h"

/// application
#include "ui/component/UiRect.h"
#include "ui/component/UiTransform.h"

/// stl
#include <algorithm>
#include <cstring>

using namespace OriGine;

namespace LogGuide {

namespace {
/// UI 用シェーダの置き場所 (アプリ側リソース。エンジン側の既定は "engine/resource/Shader").
const std::string kUiShaderDirectory = "application/resource/Shader";
} // namespace

UiRenderSystem::UiRenderSystem() : BaseRenderSystem() {}

UiRenderSystem::~UiRenderSystem() {}

void UiRenderSystem::Initialize() {
    // 先に呼ぶ。中で CreatePSO() が走る。
    BaseRenderSystem::Initialize();

    auto device = Engine::GetInstance()->GetDxDevice()->device_;
    rectBuffer_.CreateBuffer(device, kInitialRectCapacity);
    glyphBuffer_.CreateBuffer(device, kInitialGlyphCapacity);
    rectItems_.reserve(kInitialRectCapacity);
    textItems_.reserve(64);
}

void UiRenderSystem::Finalize() {
    for (auto& [font, gpu] : atlases_) {
        if (gpu.created) {
            Engine::GetInstance()->GetSrvHeap()->ReleaseDescriptor(gpu.srv);
            gpu.resource.Finalize();
            gpu.created = false;
        }
    }
    atlases_.clear();
    layoutCache_.clear();
    rectBuffer_.Finalize();
    glyphBuffer_.Finalize();
    BaseRenderSystem::Finalize();
}

/// <summary>
/// フォントの GPU アトラスを用意する。無ければ新規作成、ダーティなら再アップロードする。
/// (UiTextRenderSystem からそのまま移した)
/// </summary>
UiRenderSystem::FontAtlasGpu& UiRenderSystem::EnsureAtlas(BitmapFont* _font) {
    auto& gpu = atlases_[_font];
    if (!gpu.created) {
        CreateAtlasTexture(*_font, gpu);
    } else if (_font->IsAtlasDirty()) {
        ReuploadAtlasTexture(*_font, gpu);
    }
    return gpu;
}

/// <summary>
/// フォントアトラスの GPU テクスチャを新規作成し、CPU 側ピクセルをアップロードする。
/// engine の TextRenderSystem::CreateAtlasTexture と同じ手順だが、
/// RGBA8 へ展開せず R8_UNORM のまま上げる (アップロード量と VRAM が 1/4 で済む)。
/// (UiTextRenderSystem からそのまま移した)
/// </summary>
void UiRenderSystem::CreateAtlasTexture(BitmapFont& _font, FontAtlasGpu& _gpu) {
    int w = _font.GetAtlasWidth();
    int h = _font.GetAtlasHeight();
    const std::vector<uint8_t>& pixels = _font.GetAtlasPixels();

    auto device = Engine::GetInstance()->GetDxDevice()->device_;

    D3D12_RESOURCE_DESC texDesc = {};
    texDesc.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texDesc.Width            = w;
    texDesc.Height           = h;
    texDesc.DepthOrArraySize = 1;
    texDesc.MipLevels        = 1;
    texDesc.Format           = DXGI_FORMAT_R8_UNORM; // アトラスは 1 チャンネルなので R8 のまま上げ、PS で .r をカバレッジとして使う
    texDesc.SampleDesc.Count = 1;
    texDesc.Layout           = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    texDesc.Flags            = D3D12_RESOURCE_FLAG_NONE;

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

    device->CreateCommittedResource(
        &heapProps, D3D12_HEAP_FLAG_NONE,
        &texDesc, D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(_gpu.resource.GetResourceRef().GetAddressOf()));

    UINT64 uploadSize = 0;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint;
    device->GetCopyableFootprints(&texDesc, 0, 1, 0, &footprint, nullptr, nullptr, &uploadSize);

    DxResource uploadBuffer;
    uploadBuffer.CreateBufferResource(device, static_cast<size_t>(uploadSize));

    uint8_t* mapped = nullptr;
    uploadBuffer.GetResource()->Map(0, nullptr, reinterpret_cast<void**>(&mapped));
    for (UINT row = 0; row < static_cast<UINT>(h); ++row) {
        // R8 なので 1 行あたり w バイト (RGBA8 展開時の w * 4 ではない)
        std::memcpy(mapped + footprint.Offset + row * footprint.Footprint.RowPitch,
            pixels.data() + static_cast<size_t>(row) * static_cast<size_t>(w),
            static_cast<size_t>(w));
    }
    uploadBuffer.GetResource()->Unmap(0, nullptr);

    DxCommand uploadCmd;
    // engine 側の TextRenderSystem と同じキーを使うとコマンドリスト/キューを取り合ってしまうので別名にする
    uploadCmd.Initialize("uiTextAtlasUpload", "uiTextAtlasUpload");
    uploadCmd.Close();
    uploadCmd.CommandReset();

    auto& cmdList = uploadCmd.GetCommandList();

    D3D12_TEXTURE_COPY_LOCATION dst = {};
    dst.pResource        = _gpu.resource.GetResource().Get();
    dst.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst.SubresourceIndex = 0;

    D3D12_TEXTURE_COPY_LOCATION src = {};
    src.pResource       = uploadBuffer.GetResource().Get();
    src.Type            = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    src.PlacedFootprint = footprint;

    cmdList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource   = _gpu.resource.GetResource().Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    cmdList->ResourceBarrier(1, &barrier);

    uploadCmd.Close();
    uploadCmd.ExecuteCommandAndWait();
    uploadCmd.Finalize();
    uploadBuffer.Finalize();

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format                  = DXGI_FORMAT_R8_UNORM;
    srvDesc.ViewDimension           = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MipLevels     = 1;

    SRVEntry srvEntry(&_gpu.resource, srvDesc);
    _gpu.srv             = Engine::GetInstance()->GetSrvHeap()->CreateDescriptor(&srvEntry);
    _gpu.created         = true;
    _gpu.uploadedWidth   = w;
    _gpu.uploadedHeight  = h;

    _font.ClearAtlasDirty();
}

/// <summary>
/// フォントアトラスに新しいグリフが追加された際、既存の GPU テクスチャへ再アップロードする。
/// アトラスのサイズ自体が変わっていた場合は SRV とリソースを作り直す (GrowAtlas() が走った場合)。
/// (UiTextRenderSystem からそのまま移した)
/// </summary>
void UiRenderSystem::ReuploadAtlasTexture(BitmapFont& _font, FontAtlasGpu& _gpu) {
    int w = _font.GetAtlasWidth();
    int h = _font.GetAtlasHeight();

    bool sizeChanged = (w != _gpu.uploadedWidth || h != _gpu.uploadedHeight);

    if (sizeChanged) {
        Engine::GetInstance()->GetSrvHeap()->ReleaseDescriptor(_gpu.srv);
        _gpu.resource.Finalize();
        _gpu.created = false;
        CreateAtlasTexture(_font, _gpu);
        return;
    }

    const std::vector<uint8_t>& pixels = _font.GetAtlasPixels();
    auto device = Engine::GetInstance()->GetDxDevice()->device_;

    // device->CreateCommittedResource() で直接作ったリソースは DxResource 内部のキャッシュ
    // (resourceDesc_) が設定されないため、DxResource::GetResourceDesc() は使えない。
    // 生の ID3D12Resource::GetDesc() を使うこと。
    D3D12_RESOURCE_DESC texDesc = _gpu.resource.GetResource()->GetDesc();

    UINT64 uploadSize = 0;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint;
    device->GetCopyableFootprints(&texDesc, 0, 1, 0, &footprint, nullptr, nullptr, &uploadSize);

    DxResource uploadBuffer;
    uploadBuffer.CreateBufferResource(device, static_cast<size_t>(uploadSize));

    uint8_t* mapped = nullptr;
    uploadBuffer.GetResource()->Map(0, nullptr, reinterpret_cast<void**>(&mapped));
    for (UINT row = 0; row < static_cast<UINT>(h); ++row) {
        std::memcpy(mapped + footprint.Offset + row * footprint.Footprint.RowPitch,
            pixels.data() + static_cast<size_t>(row) * static_cast<size_t>(w),
            static_cast<size_t>(w));
    }
    uploadBuffer.GetResource()->Unmap(0, nullptr);

    DxCommand uploadCmd;
    uploadCmd.Initialize("uiTextAtlasReupload", "uiTextAtlasReupload");
    uploadCmd.Close();
    uploadCmd.CommandReset();

    auto& cmdList = uploadCmd.GetCommandList();

    D3D12_RESOURCE_BARRIER barrierToCopy = {};
    barrierToCopy.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrierToCopy.Transition.pResource   = _gpu.resource.GetResource().Get();
    barrierToCopy.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barrierToCopy.Transition.StateAfter  = D3D12_RESOURCE_STATE_COPY_DEST;
    barrierToCopy.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    cmdList->ResourceBarrier(1, &barrierToCopy);

    D3D12_TEXTURE_COPY_LOCATION dst = {};
    dst.pResource        = _gpu.resource.GetResource().Get();
    dst.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst.SubresourceIndex = 0;

    D3D12_TEXTURE_COPY_LOCATION src = {};
    src.pResource       = uploadBuffer.GetResource().Get();
    src.Type            = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    src.PlacedFootprint = footprint;

    cmdList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

    D3D12_RESOURCE_BARRIER barrierToSrv = {};
    barrierToSrv.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrierToSrv.Transition.pResource   = _gpu.resource.GetResource().Get();
    barrierToSrv.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrierToSrv.Transition.StateAfter  = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barrierToSrv.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    cmdList->ResourceBarrier(1, &barrierToSrv);

    uploadCmd.Close();
    uploadCmd.ExecuteCommandAndWait();
    uploadCmd.Finalize();
    uploadBuffer.Finalize();

    _font.ClearAtlasDirty();
}

/// <summary>
/// パイプラインステートオブジェクト（PSO）を作成する。矩形用とテキスト用の 2 つを作る。
/// </summary>
void UiRenderSystem::CreatePSO() {
    ShaderManager* shaderManager = ShaderManager::GetInstance();

    ///================================================
    /// 矩形用 (UiRectRenderSystem::CreatePSO() の中身)
    ///================================================
    // 登録済みならそれを使う (3 つの exe が同じコードを共有するため、二重生成を避ける)。
    if (shaderManager->IsRegisteredPipelineStateObj("UiRect")) {
        rectPso_ = shaderManager->GetPipelineStateObj("UiRect");
    } else {
        shaderManager->LoadShader("UiRect.VS", kUiShaderDirectory, L"vs_6_0");
        shaderManager->LoadShader("UiRect.PS", kUiShaderDirectory, L"ps_6_0");

        ShaderInformation rectShaderInfo{};
        rectShaderInfo.vsKey = "UiRect.VS";
        rectShaderInfo.psKey = "UiRect.PS";

        ///================================================
        /// RootParameter の設定
        ///================================================
        // [0] b0 : ルート定数 4 個 (screenSize, instanceOffset, padding)。
        //          矩形もランに分かれるようになったため、SV_InstanceID が常に 0 始まりである
        //          ことに対処するインスタンスオフセットが要る (UiText 側と同じ形。★)。
        //          VS で使うので visibility は ALL
        D3D12_ROOT_PARAMETER rectCbParam{};
        rectCbParam.ParameterType            = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        rectCbParam.ShaderVisibility         = D3D12_SHADER_VISIBILITY_ALL;
        rectCbParam.Constants.ShaderRegister = 0;
        rectCbParam.Constants.RegisterSpace  = 0;
        rectCbParam.Constants.Num32BitValues = 4;
        rectShaderInfo.pushBackRootParameter(rectCbParam);

        // [1] t0 : StructuredBuffer。IStructuredBuffer::SetForRootParameter が
        //          SetGraphicsRootDescriptorTable を呼ぶので DESCRIPTOR_TABLE でなければならない
        D3D12_DESCRIPTOR_RANGE rectRange{};
        rectRange.BaseShaderRegister                = 0;
        rectRange.NumDescriptors                    = 1;
        rectRange.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        rectRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        D3D12_ROOT_PARAMETER rectSrvParam{};
        rectSrvParam.ParameterType    = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        rectSrvParam.ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
        size_t rectSrvIndex           = rectShaderInfo.pushBackRootParameter(rectSrvParam);
        rectShaderInfo.SetDescriptorRange2Parameter(&rectRange, 1, rectSrvIndex);

        ///================================================
        /// RasterizerDesc / DepthStencilDesc の設定
        ///================================================
        // 入力レイアウトは push しない (頂点バッファ無しで SV_VertexID から組み立てる)。
        // サンプラーも不要 (テクスチャを使わない)。
        rectShaderInfo.changeCullMode(D3D12_CULL_MODE_NONE);

        D3D12_DEPTH_STENCIL_DESC rectDepthDesc{};
        rectDepthDesc.DepthEnable    = FALSE;
        rectDepthDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
        rectDepthDesc.DepthFunc      = D3D12_COMPARISON_FUNC_ALWAYS;
        rectDepthDesc.StencilEnable  = FALSE;
        rectShaderInfo.SetDepthStencilDesc(rectDepthDesc);

        rectShaderInfo.blendMode_ = BlendMode::Alpha;

        ///================================================
        /// 生成 (ブレンドモードごとに作らない。PSO はこれ 1 つだけ)
        ///================================================
        rectPso_ = shaderManager->CreatePso("UiRect", rectShaderInfo, Engine::GetInstance()->GetDxDevice()->device_);
    }

    ///================================================
    /// テキスト用 (UiTextRenderSystem::CreatePSO() の中身)
    ///================================================
    if (shaderManager->IsRegisteredPipelineStateObj("UiText")) {
        textPso_ = shaderManager->GetPipelineStateObj("UiText");
    } else {
        shaderManager->LoadShader("UiText.VS", kUiShaderDirectory, L"vs_6_0");
        shaderManager->LoadShader("UiText.PS", kUiShaderDirectory, L"ps_6_0");

        ShaderInformation textShaderInfo{};
        textShaderInfo.vsKey = "UiText.VS";
        textShaderInfo.psKey = "UiText.PS";

        ///================================================
        /// サンプラーの設定 (フォントアトラスをサンプリングするため)
        ///================================================
        D3D12_STATIC_SAMPLER_DESC sampler = {};
        sampler.Filter           = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        sampler.AddressU         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.AddressV         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.AddressW         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.ComparisonFunc   = D3D12_COMPARISON_FUNC_NEVER;
        sampler.MinLOD           = 0;
        sampler.MaxLOD           = D3D12_FLOAT32_MAX;
        sampler.ShaderRegister   = 0;
        sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        textShaderInfo.pushBackSamplerDesc(sampler);

        ///================================================
        /// RootParameter の設定
        ///================================================
        // [0] b0 : ルート定数 4 個 (screenSize, instanceOffset, padding)。VS/PS 両方で使うので visibility は ALL
        D3D12_ROOT_PARAMETER textCbParam{};
        textCbParam.ParameterType            = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        textCbParam.ShaderVisibility         = D3D12_SHADER_VISIBILITY_ALL;
        textCbParam.Constants.ShaderRegister = 0;
        textCbParam.Constants.RegisterSpace  = 0;
        textCbParam.Constants.Num32BitValues = 4;
        textShaderInfo.pushBackRootParameter(textCbParam);

        // [1] t0 : StructuredBuffer (グリフのインスタンス配列)。VS でのみ使う。
        //          IStructuredBuffer::SetForRootParameter が SetGraphicsRootDescriptorTable を
        //          呼ぶので DESCRIPTOR_TABLE でなければならない
        D3D12_DESCRIPTOR_RANGE instanceRange{};
        instanceRange.BaseShaderRegister                = 0;
        instanceRange.NumDescriptors                    = 1;
        instanceRange.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        instanceRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        D3D12_ROOT_PARAMETER instanceParam{};
        instanceParam.ParameterType    = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        instanceParam.ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
        size_t instanceIndex           = textShaderInfo.pushBackRootParameter(instanceParam);
        textShaderInfo.SetDescriptorRange2Parameter(&instanceRange, 1, instanceIndex);

        // [2] t1 : フォントアトラス (Texture2D)。PS でのみ使う。
        D3D12_DESCRIPTOR_RANGE atlasRange{};
        atlasRange.BaseShaderRegister                = 1;
        atlasRange.NumDescriptors                    = 1;
        atlasRange.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        atlasRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        D3D12_ROOT_PARAMETER atlasParam{};
        atlasParam.ParameterType    = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        atlasParam.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        size_t atlasIndex           = textShaderInfo.pushBackRootParameter(atlasParam);
        textShaderInfo.SetDescriptorRange2Parameter(&atlasRange, 1, atlasIndex);

        ///================================================
        /// RasterizerDesc / DepthStencilDesc の設定
        ///================================================
        // 入力レイアウトは push しない (頂点バッファ無しで SV_VertexID から組み立てる)。
        textShaderInfo.changeCullMode(D3D12_CULL_MODE_NONE);

        D3D12_DEPTH_STENCIL_DESC textDepthDesc{};
        textDepthDesc.DepthEnable    = FALSE;
        textDepthDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
        textDepthDesc.DepthFunc      = D3D12_COMPARISON_FUNC_ALWAYS;
        textDepthDesc.StencilEnable  = FALSE;
        textShaderInfo.SetDepthStencilDesc(textDepthDesc);

        textShaderInfo.blendMode_ = BlendMode::Alpha;

        ///================================================
        /// 生成 (ブレンドモードごとに作らない。PSO はこれ 1 つだけ)
        ///================================================
        textPso_ = shaderManager->CreatePso("UiText", textShaderInfo, Engine::GetInstance()->GetDxDevice()->device_);
    }
}

/// <summary>
/// レンダリング開始時の共通設定
/// </summary>
void UiRenderSystem::StartRender() {
    auto& commandList = dxCommand_->GetCommandList();
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    ID3D12DescriptorHeap* heaps[] = {Engine::GetInstance()->GetSrvHeap()->GetHeap().Get()};
    commandList->SetDescriptorHeaps(1, heaps);
}

/// <summary>
/// エンティティの UiRect / TextComponent を描画対象として登録する。
/// 矩形とテキストは独立にチェックする (早期 return で片方を落とさないこと)。
/// テキストのレイアウト計算は engine の TextLayoutSystem に委譲し、
/// 結果をエンティティ単位でキャッシュする。
/// </summary>
/// <param name="_entity">対象のエンティティハンドル</param>
void UiRenderSystem::DispatchRenderer(EntityHandle _entity) {
    UiTransform* transform = GetComponent<UiTransform>(_entity);

    // --- 矩形 ---
    if (transform != nullptr) {
        UiRect* rect = GetComponent<UiRect>(_entity);
        if (rect != nullptr && transform->visible && rect->visible) {
            UiRectInstanceData data{};
            data.rectMin      = transform->resolvedMin;
            data.rectMax      = transform->resolvedMax;
            data.fillColor    = rect->fillColor;
            data.borderColor  = rect->borderColor;
            data.cornerRadius = rect->cornerRadius;
            data.borderWidth  = rect->borderWidth;
            // 親から受け継いだクリップ矩形。UiLayoutSystem が毎フレーム解決している。
            data.clipMin = transform->clipMin;
            data.clipMax = transform->clipMax;

            rectItems_.push_back({transform->resolvedPriority, data});
        }
    }

    // --- テキスト ---
    TextComponent* text = GetComponent<TextComponent>(_entity);
    if (text != nullptr && text->visible) {
        BitmapFont* font = FontManager::GetInstance()->GetFont(text->fontHandle);
        if (font != nullptr) {
            // クリップ矩形は UiTransform が持っている。無ければ画面全体を使う。
            Vec2f clipMin{0.0f, 0.0f};
            Vec2f clipMax{0.0f, 0.0f};
            if (transform != nullptr) {
                clipMin = transform->clipMin;
                clipMax = transform->clipMax;
            } else {
                WinApp* window = Engine::GetInstance()->GetWinApp();
                clipMax = {static_cast<float>(window->GetWidth()),
                           static_cast<float>(window->GetHeight())};
            }

            // unordered_map の要素への参照は rehash されても無効化されないので、アドレスを保持してよい。
            TextLayoutResult& cached = layoutCache_[_entity];
            layout_.UpdateLayout(*font, *text, cached, true); // ここで TextComponent::dirty を消費する
            if (!cached.quads.empty()) {
                textItems_.push_back({text->renderPriority, font, text, &cached, clipMin, clipMax});
            }
        }
    }
}

/// <summary>
/// レンダリングをスキップするかどうかを判定する
/// </summary>
/// <returns>true = 描画対象なし / false = 描画対象あり</returns>
bool UiRenderSystem::ShouldSkipRender() const {
    return rectItems_.empty() && textItems_.empty();
}

/// <summary>
/// 矩形とテキストのレンダリング本体。
/// 別々のパスに分けず、resolvedPriority で 1 本の並びにまとめてから
/// PSO を切り替えて順に描く (ImGui のコマンドリストと同じ考え方)。
/// これにより「全矩形 → 全テキスト」の 2 パスによる前後関係の破綻を防ぐ。
/// </summary>
void UiRenderSystem::Rendering() {
    // --- 使用する全フォントのアトラスを用意する (UiTextRenderSystem と同じロジック) ---
    // あわせて、アトラスが作り直されたかどうかを見ておく。
    bool atlasRebuilt = false;
    for (const TextItem& item : textItems_) {
        auto atlasItr    = atlases_.find(item.font);
        const bool known = (atlasItr != atlases_.end() && atlasItr->second.created);
        const int prevW  = known ? atlasItr->second.uploadedWidth : -1;
        const int prevH  = known ? atlasItr->second.uploadedHeight : -1;

        const FontAtlasGpu& gpu = EnsureAtlas(item.font);
        if (gpu.uploadedWidth != prevW || gpu.uploadedHeight != prevH) {
            atlasRebuilt = true;
        }
    }

    if (atlasRebuilt) {
        // BitmapFont::GrowAtlas() はアトラスを 2 倍にしたあと ResetPacker() して
        // 全グリフを再パックするため、既存グリフの UV もすべて変わる。
        // このフレームで既に計算済みのレイアウトは古い UV を持っているので、作り直す。
        //
        // TextLayoutSystem::UpdateLayout は「dirty でなく valid なら早期 return」する。
        // dirty は DispatchRenderer で消費済みなので、valid を落とさないと二度と
        // 再計算されず、文字が崩れたまま固定されてしまう。
        for (auto& [entity, cachedLayout] : layoutCache_) {
            cachedLayout.valid = false;
        }
        for (TextItem& item : textItems_) {
            // dirty は既に消費しているので、ここでは消費しない。
            layout_.UpdateLayout(*item.font, *item.text, *item.layout, false);
        }
    }

    // --- 矩形とテキストを 1 本の並びにする ---
    // 別々のシステムに分けると「全矩形 → 全テキスト」の 2 パスになり、
    // 奥のウィンドウのテキストが手前のウィンドウの矩形の上に出てしまう。
    // ここで両方を resolvedPriority で一緒に並べる。
    commands_.clear();
    commands_.reserve(rectItems_.size() + textItems_.size());
    for (uint32_t i = 0; i < rectItems_.size(); ++i) {
        commands_.push_back({rectItems_[i].priority, DrawKind::Rect, i});
    }
    for (uint32_t i = 0; i < textItems_.size(); ++i) {
        commands_.push_back({textItems_[i].priority, DrawKind::Text, i});
    }

    // 優先度の昇順 (小さいものが先＝奥)。同じ優先度なら矩形が先
    // (自分の背景の上に自分の文字が乗る。UiLayoutSystem が TextComponent::renderPriority に
    // UiTransform::resolvedPriority と同じ値を入れているため、要素内では必ず同値になる)。
    std::stable_sort(commands_.begin(), commands_.end(),
        [](const DrawCommand& _a, const DrawCommand& _b) {
            if (_a.priority != _b.priority) {
                return _a.priority < _b.priority;
            }
            return _a.kind < _b.kind;
        });

    // --- 並び順にインスタンスを積みつつ、連続する同種をドローランにまとめる ---
    rectBuffer_.openData_.clear();
    glyphBuffer_.openData_.clear();
    runs_.clear();

    for (const DrawCommand& command : commands_) {
        if (command.kind == DrawKind::Rect) {
            const uint32_t first = static_cast<uint32_t>(rectBuffer_.openData_.size());
            rectBuffer_.openData_.push_back(rectItems_[command.itemIndex].data);

            // 直前も矩形なら同じランに足す
            if (!runs_.empty() && runs_.back().kind == DrawKind::Rect) {
                ++runs_.back().instanceCount;
            } else {
                runs_.push_back({DrawKind::Rect, nullptr, first, 1});
            }
        } else {
            const TextItem& item = textItems_[command.itemIndex];
            const uint32_t first = static_cast<uint32_t>(glyphBuffer_.openData_.size());
            for (const GlyphQuad& quad : item.layout->quads) {
                UiGlyphInstanceData data{};
                data.posMin  = quad.posMin;
                data.posMax  = quad.posMax;
                data.uvMin   = quad.uvMin;
                data.uvMax   = quad.uvMax;
                data.color   = quad.color;
                data.clipMin = item.clipMin;
                data.clipMax = item.clipMax;
                glyphBuffer_.openData_.push_back(data);
            }
            const uint32_t count = static_cast<uint32_t>(item.layout->quads.size());

            // 直前もテキストで、しかも同じフォントなら同じランに足す
            // (フォントが違うとアトラスの差し替えが要るのでランを分ける)
            if (!runs_.empty() && runs_.back().kind == DrawKind::Text &&
                runs_.back().font == item.font) {
                runs_.back().instanceCount += count;
            } else {
                runs_.push_back({DrawKind::Text, item.font, first, count});
            }
        }
    }

    // --- バッファのアップロード ---
    auto device = Engine::GetInstance()->GetDxDevice()->device_;
    // Resize() は要素数が「一致しない」だけで作り直す (ヘッダのコメントとは裏腹に、縮小でも走る) ので、
    // 足りないときだけ余裕を持たせて明示的に広げる。ConvertToBuffer() は境界チェックをしないので、
    // この判定を省いてはいけない。
    if (rectBuffer_.openData_.size() > rectBuffer_.Capacity()) {
        rectBuffer_.Resize(device, static_cast<uint32_t>(rectBuffer_.openData_.size() * 2));
    }
    if (glyphBuffer_.openData_.size() > glyphBuffer_.Capacity()) {
        glyphBuffer_.Resize(device, static_cast<uint32_t>(glyphBuffer_.openData_.size() * 2));
    }
    // ShouldSkipRender() は「両方空」でしか true にならないため、片方だけが空のフレームで
    // ここに来ることがある。空のバッファに対して ConvertToBuffer() を呼んでも実害はないが、
    // 意図を明確にするため明示的にガードする。
    if (!rectBuffer_.openData_.empty()) {
        rectBuffer_.ConvertToBuffer();
    }
    if (!glyphBuffer_.openData_.empty()) {
        glyphBuffer_.ConvertToBuffer();
    }

    // --- ランの順に描く ---
    StartRender();

    auto& commandList = dxCommand_->GetCommandList();
    WinApp* window            = Engine::GetInstance()->GetWinApp();
    const float screenWidth   = static_cast<float>(window->GetWidth());
    const float screenHeight  = static_cast<float>(window->GetHeight());

    // SV_InstanceID は常に 0 始まりなので、ランの開始位置はルート定数で渡す。
    // 矩形もランに分かれるようになったため、テキスト側と同じ形にしてある
    // (UiRect.hlsli / UiRect.VS.hlsl 側もこれに合わせてルート定数を 4 個に拡張済み)。
    struct RectRootConstants {
        float screenWidth       = 0.0f;
        float screenHeight      = 0.0f;
        uint32_t instanceOffset = 0;
        uint32_t padding        = 0;
    };
    static_assert(sizeof(RectRootConstants) == 16, "ルート定数は 4 x 32bit");
    struct TextRootConstants {
        float screenWidth       = 0.0f;
        float screenHeight      = 0.0f;
        uint32_t instanceOffset = 0;
        uint32_t padding        = 0;
    };
    static_assert(sizeof(TextRootConstants) == 16, "ルート定数は 4 x 32bit");

    for (const DrawRun& run : runs_) {
        if (run.instanceCount == 0) {
            continue;
        }
        if (run.kind == DrawKind::Rect) {
            // ルートシグネチャを切り替えるとルート引数 (ルート定数もディスクリプタテーブルも) は
            // 無効になるので、ランごとに毎回すべて設定し直す。
            commandList->SetGraphicsRootSignature(rectPso_->rootSignature.Get());
            commandList->SetPipelineState(rectPso_->pipelineState.Get());

            RectRootConstants rc{};
            rc.screenWidth    = screenWidth;
            rc.screenHeight   = screenHeight;
            rc.instanceOffset = run.firstInstance; // SV_InstanceID は常に 0 始まりなのでここで渡す
            commandList->SetGraphicsRoot32BitConstants(0, 4, &rc, 0);
            rectBuffer_.SetForRootParameter(commandList, 1);

            commandList->DrawInstanced(6, run.instanceCount, 0, 0);
        } else {
            auto atlasItr = atlases_.find(run.font);
            if (atlasItr == atlases_.end() || !atlasItr->second.created) {
                continue;
            }
            commandList->SetGraphicsRootSignature(textPso_->rootSignature.Get());
            commandList->SetPipelineState(textPso_->pipelineState.Get());

            TextRootConstants rc{};
            rc.screenWidth    = screenWidth;
            rc.screenHeight   = screenHeight;
            rc.instanceOffset = run.firstInstance; // SV_InstanceID は常に 0 始まりなのでここで渡す
            commandList->SetGraphicsRoot32BitConstants(0, 4, &rc, 0);
            glyphBuffer_.SetForRootParameter(commandList, 1);
            commandList->SetGraphicsRootDescriptorTable(2, atlasItr->second.srv.GetGpuHandle());

            commandList->DrawInstanced(6, run.instanceCount, 0, 0);
        }
    }

    // 毎フレームの収集リストは自分でクリアする (フレームワークはやってくれない)
    rectItems_.clear();
    textItems_.clear();
}

} // namespace LogGuide
