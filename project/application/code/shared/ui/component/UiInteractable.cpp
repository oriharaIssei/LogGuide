#include "ui/component/UiInteractable.h"

#ifdef _DEBUG
#include <imgui/imgui.h>
#endif // _DEBUG

using namespace OriGine;

namespace LogGuide {

void UiInteractable::Initialize([[maybe_unused]] Scene* _scene, [[maybe_unused]] const EntityHandle& _owner) {}

void UiInteractable::Finalize() {}

void UiInteractable::Edit([[maybe_unused]] Scene* _scene, [[maybe_unused]] const EntityHandle& _owner, [[maybe_unused]] const std::string& _parentLabel) {
#ifdef _DEBUG
    std::string label = _parentLabel + "##UiInteractable";

    ImGui::Checkbox(("Enabled" + label).c_str(), &enabled);
    // 実行時の状態は UiInteractionSystem が書き込むだけなので、ここでは読み取り専用表示にする。
    ImGui::Text("hover:%d press:%d click:%d", static_cast<int>(isHovered), static_cast<int>(isPressed), static_cast<int>(wasClicked));
#endif // _DEBUG
}

void to_json(nlohmann::json& _j, const UiInteractable& _c) {
    _j["enabled"] = _c.enabled;
}

void from_json(const nlohmann::json& _j, UiInteractable& _c) {
    if (_j.contains("enabled")) { _j["enabled"].get_to(_c.enabled); }
}

} // namespace LogGuide
