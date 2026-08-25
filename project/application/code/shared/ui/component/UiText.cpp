#include "ui/component/UiText.h"

#ifdef _DEBUG
#include <imgui/imgui.h>
#endif // _DEBUG

using namespace OriGine;

namespace LogGuide {

void UiText::Initialize([[maybe_unused]] Scene* _scene, [[maybe_unused]] EntityHandle _owner) {}

void UiText::Finalize() {}

void UiText::Edit([[maybe_unused]] Scene* _scene, [[maybe_unused]] EntityHandle _owner, [[maybe_unused]] const std::string& _parentLabel) {
#ifdef _DEBUG
    std::string label = _parentLabel + "##UiText";

    ImGui::DragFloat4(("Padding" + label).c_str(), &padding[X], 0.5f, 0.0f, 256.0f);

    const char* verticalAlignNames[] = {"Top", "Middle", "Bottom"};
    int verticalAlignIdx             = static_cast<int>(verticalAlign);
    if (ImGui::Combo(("VerticalAlign" + label).c_str(), &verticalAlignIdx, verticalAlignNames, 3)) {
        verticalAlign = static_cast<UiTextVerticalAlign>(verticalAlignIdx);
    }
#endif // _DEBUG
}

void to_json(nlohmann::json& _j, const UiText& _c) {
    _j["padding"]       = {_c.padding[X], _c.padding[Y], _c.padding[Z], _c.padding[W]};
    _j["verticalAlign"] = static_cast<int>(_c.verticalAlign);
}

void from_json(const nlohmann::json& _j, UiText& _c) {
    if (_j.contains("padding")) { _j["padding"].get_to(_c.padding); }
    if (_j.contains("verticalAlign")) {
        int v = 0;
        _j["verticalAlign"].get_to(v);
        _c.verticalAlign = static_cast<UiTextVerticalAlign>(v);
    }
}

} // namespace LogGuide
