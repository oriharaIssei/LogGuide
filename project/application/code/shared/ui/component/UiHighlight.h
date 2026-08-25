#pragma once

#include "component/IComponent.h"

#include <string>
#include <Vector4.h>

namespace LogGuide {

/// UiInteractable の状態に応じて UiRect::fillColor を差し替えるコンポーネント.
/// UiInteractable と UiRect の両方を持つエンティティに付ける.
class UiHighlight final : public OriGine::IComponent {
public:
    UiHighlight()           = default;
    ~UiHighlight() override = default;

    void Initialize(OriGine::Scene* _scene, OriGine::EntityHandle _owner) override;
    void Finalize() override;
    void Edit(OriGine::Scene* _scene, OriGine::EntityHandle _owner, const std::string& _parentLabel) override;

    OriGine::Vec4f normalColor   = {0.16f, 0.17f, 0.20f, 1.0f};
    OriGine::Vec4f hoverColor    = {0.24f, 0.26f, 0.32f, 1.0f};
    OriGine::Vec4f pressedColor  = {0.12f, 0.13f, 0.16f, 1.0f};
    OriGine::Vec4f disabledColor = {0.14f, 0.14f, 0.15f, 1.0f};
};

void to_json(nlohmann::json& _j, const UiHighlight& _c);
void from_json(const nlohmann::json& _j, UiHighlight& _c);

} // namespace LogGuide
