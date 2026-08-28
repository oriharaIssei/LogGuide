#pragma once

#include "component/IComponent.h"

#include <string>
#include <Vector4.h>

namespace LogGuide {

/// UI 要素の見た目 (塗り・角丸・枠線).
/// UiTransform と組み合わせて使う.
class UiRect final : public OriGine::IComponent {
public:
    UiRect()           = default;
    ~UiRect() override = default;

    void Initialize(OriGine::Scene* _scene, const OriGine::EntityHandle& _owner) override;
    void Finalize() override;
    void Edit(OriGine::Scene* _scene, const OriGine::EntityHandle& _owner, const std::string& _parentLabel) override;

    OriGine::Vec4f fillColor    = {0.16f, 0.17f, 0.20f, 1.0f};
    OriGine::Vec4f borderColor  = {0.40f, 0.42f, 0.48f, 1.0f};
    /// 各隅の半径(px). CSS の border-radius と同じ順で (左上, 右上, 右下, 左下).
    OriGine::Vec4f cornerRadius = {12.0f, 12.0f, 12.0f, 12.0f};
    float borderWidth           = 2.0f;

    bool visible = true;
};

void to_json(nlohmann::json& _j, const UiRect& _c);
void from_json(const nlohmann::json& _j, UiRect& _c);

} // namespace LogGuide
