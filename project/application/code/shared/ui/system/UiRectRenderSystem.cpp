#include "ui/system/UiRectRenderSystem.h"

/// engine
#include "Engine.h"
#include "directX12/DxCommand.h"
// Engine.h は DxDevice を前方宣言しているだけなので、device_ を触るには定義が要る
#include "directX12/DxDevice.h"
#include "directX12/ShaderManager.h"
#include "winApp/WinApp.h"

/// application
#include "ui/component/UiRect.h"
#include "ui/component/UiTransform.h"

/// stl
#include <algorithm>

using namespace OriGine;

namespace LogGuide {

namespace {
/// UiRect 用シェーダの置き場所 (アプリ側リソース。エンジン側の既定は "engine/resource/Shader").
const std::string kUiShaderDirectory = "application/resource/Shader";
} // namespace

UiRectRenderSystem::UiRectRenderSystem() : BaseRenderSystem() {}

UiRectRenderSystem::~UiRectRenderSystem() {}

void UiRectRenderSystem::Initialize() {
    // 先に呼ぶ。中で CreatePSO() が走る。
    BaseRenderSystem::Initialize();

    instanceBuffer_.CreateBuffer(Engine::GetInstance()->GetDxDevice()->device_, kInitialInstanceCapacity);
    collected_.reserve(kInitialInstanceCapacity);
}

void UiRectRenderSystem::Finalize() {
    instanceBuffer_.Finalize();
    BaseRenderSystem::Finalize();
}

/// <summary>
/// パイプラインステートオブジェクト（PSO）を作成する
/// </summary>
void UiRectRenderSystem::CreatePSO() {
    ShaderManager* shaderManager = ShaderManager::GetInstance();

    // 登録済みならそれを使う (3 つの exe が同じコードを共有するため、二重生成を避ける)。
    if (shaderManager->IsRegisteredPipelineStateObj("UiRect")) {
        pso_ = shaderManager->GetPipelineStateObj("UiRect");
        return;
    }

    shaderManager->LoadShader("UiRect.VS", kUiShaderDirectory, L"vs_6_0");
    shaderManager->LoadShader("UiRect.PS", kUiShaderDirectory, L"ps_6_0");

    ShaderInformation shaderInfo{};
    shaderInfo.vsKey = "UiRect.VS";
    shaderInfo.psKey = "UiRect.PS";

    ///================================================
    /// RootParameter の設定
    ///================================================
    // [0] b0 : ルート定数 2 個 (screenSize)。VS で使うので visibility は ALL
    D3D12_ROOT_PARAMETER cbParam{};
    cbParam.ParameterType            = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    cbParam.ShaderVisibility         = D3D12_SHADER_VISIBILITY_ALL;
    cbParam.Constants.ShaderRegister = 0;
    cbParam.Constants.RegisterSpace  = 0;
    cbParam.Constants.Num32BitValues = 2;
    shaderInfo.pushBackRootParameter(cbParam);

    // [1] t0 : StructuredBuffer。IStructuredBuffer::SetForRootParameter が
    //          SetGraphicsRootDescriptorTable を呼ぶので DESCRIPTOR_TABLE でなければならない
    D3D12_DESCRIPTOR_RANGE range{};
    range.BaseShaderRegister                = 0;
    range.NumDescriptors                    = 1;
    range.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER srvParam{};
    srvParam.ParameterType    = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    srvParam.ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    size_t srvIndex           = shaderInfo.pushBackRootParameter(srvParam);
    shaderInfo.SetDescriptorRange2Parameter(&range, 1, srvIndex);

    ///================================================
    /// RasterizerDesc / DepthStencilDesc の設定
    ///================================================
    // 入力レイアウトは push しない (頂点バッファ無しで SV_VertexID から組み立てる)。
    // サンプラーも不要 (テクスチャを使わない)。
    shaderInfo.changeCullMode(D3D12_CULL_MODE_NONE);

    D3D12_DEPTH_STENCIL_DESC depthDesc{};
    depthDesc.DepthEnable    = FALSE;
    depthDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    depthDesc.DepthFunc      = D3D12_COMPARISON_FUNC_ALWAYS;
    depthDesc.StencilEnable  = FALSE;
    shaderInfo.SetDepthStencilDesc(depthDesc);

    shaderInfo.blendMode_ = BlendMode::Alpha;

    ///================================================
    /// 生成 (ブレンドモードごとに作らない。PSO はこれ 1 つだけ)
    ///================================================
    pso_ = shaderManager->CreatePso("UiRect", shaderInfo, Engine::GetInstance()->GetDxDevice()->device_);
}

/// <summary>
/// レンダリング開始時の共通設定
/// </summary>
void UiRectRenderSystem::StartRender() {
    auto& commandList = dxCommand_->GetCommandList();
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    ID3D12DescriptorHeap* heaps[] = {Engine::GetInstance()->GetSrvHeap()->GetHeap().Get()};
    commandList->SetDescriptorHeaps(1, heaps);
}

/// <summary>
/// エンティティの UiRect を描画対象として登録する
/// </summary>
/// <param name="_entity">対象のエンティティハンドル</param>
void UiRectRenderSystem::DispatchRenderer(EntityHandle _entity) {
    UiTransform* transform = GetComponent<UiTransform>(_entity);
    UiRect* rect           = GetComponent<UiRect>(_entity);
    if (!transform || !rect || !transform->visible || !rect->visible) {
        return;
    }

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

    collected_.push_back({transform->renderPriority, data});
}

/// <summary>
/// レンダリングをスキップするかどうかを判定する
/// </summary>
/// <returns>true = 描画対象なし / false = 描画対象あり</returns>
bool UiRectRenderSystem::ShouldSkipRender() const {
    return collected_.empty();
}

/// <summary>
/// 角丸矩形のレンダリング本体 (1 ドローコールでまとめて描く)
/// </summary>
void UiRectRenderSystem::Rendering() {
    // renderPriority の昇順 (小さいものが先＝奥) に並べる。
    // アルファブレンドは描画順に依存するので必ずソートする。
    std::stable_sort(
        collected_.begin(),
        collected_.end(),
        [](const auto& _a, const auto& _b) { return _a.first < _b.first; });

    StartRender();

    auto& commandList = dxCommand_->GetCommandList();
    auto device        = Engine::GetInstance()->GetDxDevice()->device_;

    instanceBuffer_.openData_.clear();
    for (const auto& [priority, data] : collected_) {
        instanceBuffer_.openData_.push_back(data);
    }
    // IStructuredBuffer::ResizeForDataSize() は要素数が「一致しない」だけでバッファと SRV を
    // 作り直す (ヘッダのコメントとは裏腹に、縮小でも走る)。容量 256 に対して矩形 1 枚なら
    // 毎フレーム再生成が走り、リソース破棄と DxResource::Finalize() の LOG_CRITICAL が
    // 出続けてしまう。そのため、容量が足りないときだけ余裕を持たせて明示的に広げる。
    // ConvertToBuffer() は境界チェックをしないので、この判定を省いてはいけない。
    const size_t neededCount = instanceBuffer_.openData_.size();
    if (neededCount > instanceBuffer_.Capacity()) {
        instanceBuffer_.Resize(device, static_cast<uint32_t>(neededCount * 2));
    }
    instanceBuffer_.ConvertToBuffer();

    commandList->SetGraphicsRootSignature(pso_->rootSignature.Get());
    commandList->SetPipelineState(pso_->pipelineState.Get());

    WinApp* window             = Engine::GetInstance()->GetWinApp();
    const float screenSize[2] = {static_cast<float>(window->GetWidth()), static_cast<float>(window->GetHeight())};
    commandList->SetGraphicsRoot32BitConstants(0, 2, screenSize, 0);

    instanceBuffer_.SetForRootParameter(commandList, 1);

    commandList->DrawInstanced(6, static_cast<UINT>(collected_.size()), 0, 0);

    // 毎フレームの収集リストは自分でクリアする (フレームワークはやってくれない)
    collected_.clear();
}

} // namespace LogGuide
