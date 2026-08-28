#pragma once

#include "system/ISystem.h"

namespace LogGuide {

/// UiInteractable の状態 (hover/press/enabled) に応じて UiRect::fillColor を書き換える.
/// 入力 → 状態遷移 → レイアウト → 描画 の順になるよう StateTransition カテゴリで動く
/// (UiInteractionSystem が Input カテゴリで状態を確定させたあとに読む).
class UiHighlightSystem final : public OriGine::ISystem {
public:
    UiHighlightSystem() : OriGine::ISystem(OriGine::SystemCategory::StateTransition) {}
    ~UiHighlightSystem() override = default;

    void Initialize() override;
    void Finalize() override;

protected:
    void UpdateEntity(const OriGine::EntityHandle& _entity) override;
};

} // namespace LogGuide
