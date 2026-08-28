#include "ui/component/UiTransform.h"

#ifdef _DEBUG
#include <imgui/imgui.h>
#endif // _DEBUG

using namespace OriGine;

namespace LogGuide {

void UiTransform::Initialize([[maybe_unused]] Scene* _scene, [[maybe_unused]] const EntityHandle& _owner) {}

void UiTransform::Finalize() {}

void UiTransform::Edit([[maybe_unused]] Scene* _scene, [[maybe_unused]] const EntityHandle& _owner, [[maybe_unused]] const std::string& _parentLabel) {
#ifdef _DEBUG
    std::string label = _parentLabel + "##UiTransform";

    ImGui::DragFloat2(("AnchorMin" + label).c_str(), &anchorMin[X], 0.01f, 0.0f, 1.0f);
    ImGui::DragFloat2(("AnchorMax" + label).c_str(), &anchorMax[X], 0.01f, 0.0f, 1.0f);
    ImGui::DragFloat2(("OffsetMin" + label).c_str(), &offsetMin[X], 1.0f);
    ImGui::DragFloat2(("OffsetMax" + label).c_str(), &offsetMax[X], 1.0f);
    ImGui::DragInt(("Priority" + label).c_str(), &renderPriority);
    ImGui::Checkbox(("Visible" + label).c_str(), &visible);
    ImGui::Checkbox(("ClipChildren" + label).c_str(), &clipChildren);
    ImGui::DragInt(("SurfaceId" + label).c_str(), &surfaceId, 1.0f, 0, 64);
    // parent は uuid なので編集 UI は作らない。有効/無効だけ読み取り専用で出す。
    ImGui::Text("Parent:%s", parent.IsValid() ? "set" : "none");
    // resolvedPriority / resolvedSurfaceId / resolvedVisible は UiLayoutSystem が
    // 毎フレーム書き込むだけなので、読み取り専用表示にする。
    ImGui::Text("ResolvedPriority:%d ResolvedSurfaceId:%d ResolvedVisible:%d",
        resolvedPriority, resolvedSurfaceId, static_cast<int>(resolvedVisible));
#endif // _DEBUG
}

void to_json(nlohmann::json& _j, const UiTransform& _c) {
    _j["anchorMin"]      = {_c.anchorMin[X], _c.anchorMin[Y]};
    _j["anchorMax"]      = {_c.anchorMax[X], _c.anchorMax[Y]};
    _j["offsetMin"]      = {_c.offsetMin[X], _c.offsetMin[Y]};
    _j["offsetMax"]      = {_c.offsetMax[X], _c.offsetMax[Y]};
    _j["renderPriority"] = _c.renderPriority;
    _j["visible"]        = _c.visible;
    _j["parent"]         = _c.parent;
    _j["clipChildren"]   = _c.clipChildren;
    _j["surfaceId"]      = _c.surfaceId;
}

void from_json(const nlohmann::json& _j, UiTransform& _c) {
    if (_j.contains("anchorMin"))      { _j["anchorMin"].get_to(_c.anchorMin); }
    if (_j.contains("anchorMax"))      { _j["anchorMax"].get_to(_c.anchorMax); }
    if (_j.contains("offsetMin"))      { _j["offsetMin"].get_to(_c.offsetMin); }
    if (_j.contains("offsetMax"))      { _j["offsetMax"].get_to(_c.offsetMax); }
    if (_j.contains("renderPriority")) { _j["renderPriority"].get_to(_c.renderPriority); }
    if (_j.contains("visible"))        { _j["visible"].get_to(_c.visible); }
    if (_j.contains("parent"))         { _j["parent"].get_to(_c.parent); }
    if (_j.contains("clipChildren"))   { _j["clipChildren"].get_to(_c.clipChildren); }
    if (_j.contains("surfaceId"))      { _j["surfaceId"].get_to(_c.surfaceId); }
}

} // namespace LogGuide
