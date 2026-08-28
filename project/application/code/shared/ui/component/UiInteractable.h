#pragma once

#include "component/IComponent.h"

#include <string>

namespace LogGuide {

/// UiTransform の矩形をマウス操作の対象にするコンポーネント.
/// 当たり判定と状態だけを持ち、見た目には関与しない
/// （見た目を変えたい場合は UiHighlight を併せて付ける）.
class UiInteractable final : public OriGine::IComponent {
public:
    UiInteractable()           = default;
    ~UiInteractable() override = default;

    void Initialize(OriGine::Scene* _scene, const OriGine::EntityHandle& _owner) override;
    void Finalize() override;
    void Edit(OriGine::Scene* _scene, const OriGine::EntityHandle& _owner, const std::string& _parentLabel) override;

    /// false なら一切反応しない（見た目も UiHighlight の disabledColor になる）
    bool enabled = true;

    // --- 実行時の状態 (UiInteractionSystem が書き込む。JSON には保存しない) ---
    /// カーソルが乗っている（かつ手前に他の要素が無い）
    bool isHovered = false;
    /// この要素の上で押し下げられ、まだ離されていない
    bool isPressed = false;
    /// このフレームでクリックが成立した（押した要素の上で離された）。1 フレームだけ true
    bool wasClicked = false;
};

void to_json(nlohmann::json& _j, const UiInteractable& _c);
void from_json(const nlohmann::json& _j, UiInteractable& _c);

} // namespace LogGuide
