#pragma once

#include "system/ISystem.h"

namespace LogGuide {

/// UiTransform + UiInteractable を持つエンティティのヒットテストと押下状態の更新を行う.
/// 「全要素を見てから最前面を決める」2 パスが要るため、
/// UpdateEntity() ではなく Update() をオーバーライドする.
class UiInteractionSystem final : public OriGine::ISystem {
public:
    UiInteractionSystem() : OriGine::ISystem(OriGine::SystemCategory::Input) {}
    ~UiInteractionSystem() override = default;

    void Initialize() override;
    void Finalize() override;

    void Update() override;
};

} // namespace LogGuide
