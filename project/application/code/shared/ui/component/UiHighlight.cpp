#include "ui/component/UiHighlight.h"

#ifdef _DEBUG
#include <imgui/imgui.h>
#endif // _DEBUG

using namespace OriGine;

namespace LogGuide {

void UiHighlight::Initialize([[maybe_unused]] Scene* _scene, [[maybe_unused]] const EntityHandle& _owner) {}

void UiHighlight::Finalize() {}

void UiHighlight::Edit([[maybe_unused]] Scene* _scene, [[maybe_unused]] const EntityHandle& _owner, [[maybe_unused]] const std::string& _parentLabel) {
#ifdef _DEBUG
    std::string label = _parentLabel + "##UiHighlight";

    ImGui::ColorEdit4(("NormalColor" + label).c_str(), &normalColor[X]);
    ImGui::ColorEdit4(("HoverColor" + label).c_str(), &hoverColor[X]);
    ImGui::ColorEdit4(("PressedColor" + label).c_str(), &pressedColor[X]);
    ImGui::ColorEdit4(("DisabledColor" + label).c_str(), &disabledColor[X]);
    ImGui::ColorEdit4(("SelectedColor" + label).c_str(), &selectedColor[X]);
#endif // _DEBUG
}

void to_json(nlohmann::json& _j, const UiHighlight& _c) {
    _j["normalColor"]   = {_c.normalColor[X], _c.normalColor[Y], _c.normalColor[Z], _c.normalColor[W]};
    _j["hoverColor"]    = {_c.hoverColor[X], _c.hoverColor[Y], _c.hoverColor[Z], _c.hoverColor[W]};
    _j["pressedColor"]  = {_c.pressedColor[X], _c.pressedColor[Y], _c.pressedColor[Z], _c.pressedColor[W]};
    _j["disabledColor"] = {_c.disabledColor[X], _c.disabledColor[Y], _c.disabledColor[Z], _c.disabledColor[W]};
    _j["selectedColor"] = {_c.selectedColor[X], _c.selectedColor[Y], _c.selectedColor[Z], _c.selectedColor[W]};
}

void from_json(const nlohmann::json& _j, UiHighlight& _c) {
    if (_j.contains("normalColor"))   { _j["normalColor"].get_to(_c.normalColor); }
    if (_j.contains("hoverColor"))    { _j["hoverColor"].get_to(_c.hoverColor); }
    if (_j.contains("pressedColor"))  { _j["pressedColor"].get_to(_c.pressedColor); }
    if (_j.contains("disabledColor")) { _j["disabledColor"].get_to(_c.disabledColor); }
    if (_j.contains("selectedColor")) { _j["selectedColor"].get_to(_c.selectedColor); }
}

} // namespace LogGuide
