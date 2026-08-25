#pragma once

/// parent
#include "system/render/base/BaseRenderSystem.h"

/// engine
#include "directX12/PipelineStateObj.h"
#include "directX12/buffer/IStructuredBuffer.h"

/// application
#include "ui/UiRectInstanceData.h"

/// stl
#include <cstdint>
#include <utility>
#include <vector>

namespace LogGuide {

/// UiTransform + UiRect を持つエンティティを集め、角丸矩形として 1 ドローコールで描く.
/// 頂点バッファは使わず、StructuredBuffer + SV_VertexID / SV_InstanceID で組み立てる.
class UiRectRenderSystem final : public OriGine::BaseRenderSystem {
public:
    UiRectRenderSystem();
    ~UiRectRenderSystem() override;

    void Initialize() override;
    void Finalize() override;

    void CreatePSO() override;
    void StartRender() override;
    void Rendering() override;
    void DispatchRenderer(OriGine::EntityHandle _entity) override;
    bool ShouldSkipRender() const override;

private:
    static constexpr uint32_t kInitialInstanceCapacity = 256;

    OriGine::PipelineStateObj* pso_ = nullptr;
    OriGine::IStructuredBuffer<UiRectInstanceData> instanceBuffer_;
    /// このフレームに描く矩形. first = UiTransform::renderPriority.
    std::vector<std::pair<int32_t, UiRectInstanceData>> collected_;
};

} // namespace LogGuide
