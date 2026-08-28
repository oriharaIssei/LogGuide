#include "ui/component/UiRect.h"

#ifdef _DEBUG
#include <imgui/imgui.h>
#endif // _DEBUG

using namespace OriGine;

namespace LogGuide {

void UiRect::Initialize([[maybe_unused]] Scene* _scene, [[maybe_unused]] const EntityHandle& _owner) {}

void UiRect::Finalize() {}

void UiRect::Edit([[maybe_unused]] Scene* _scene, [[maybe_unused]] const EntityHandle& _owner, [[maybe_unused]] const std::string& _parentLabel) {
#ifdef _DEBUG
    std::string label = _parentLabel + "##UiRect";

    ImGui::ColorEdit4(("FillColor" + label).c_str(), &fillColor[X]);
    ImGui::ColorEdit4(("BorderColor" + label).c_str(), &borderColor[X]);
    ImGui::DragFloat4(("CornerRadius" + label).c_str(), &cornerRadius[X], 0.5f, 0.0f, 512.0f);
    ImGui::DragFloat(("BorderWidth" + label).c_str(), &borderWidth, 0.1f, 0.0f, 64.0f);
    ImGui::Checkbox(("Visible" + label).c_str(), &visible);
#endif // _DEBUG
}

void to_json(nlohmann::json& _j, const UiRect& _c) {
    _j["fillColor"]    = {_c.fillColor[X], _c.fillColor[Y], _c.fillColor[Z], _c.fillColor[W]};
    _j["borderColor"]  = {_c.borderColor[X], _c.borderColor[Y], _c.borderColor[Z], _c.borderColor[W]};
    _j["cornerRadius"] = {_c.cornerRadius[X], _c.cornerRadius[Y], _c.cornerRadius[Z], _c.cornerRadius[W]};
    _j["borderWidth"]  = _c.borderWidth;
    _j["visible"]      = _c.visible;
}

void from_json(const nlohmann::json& _j, UiRect& _c) {
    if (_j.contains("fillColor"))    { _j["fillColor"].get_to(_c.fillColor); }
    if (_j.contains("borderColor"))  { _j["borderColor"].get_to(_c.borderColor); }
    if (_j.contains("cornerRadius")) { _j["cornerRadius"].get_to(_c.cornerRadius); }
    if (_j.contains("borderWidth"))  { _j["borderWidth"].get_to(_c.borderWidth); }
    if (_j.contains("visible"))      { _j["visible"].get_to(_c.visible); }
}

} // namespace LogGuide
