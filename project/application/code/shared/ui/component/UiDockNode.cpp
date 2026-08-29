#include "ui/component/UiDockNode.h"

#ifdef _DEBUG
#include <imgui/imgui.h>
#endif // _DEBUG

using namespace OriGine;

namespace LogGuide {

void UiDockNode::Initialize([[maybe_unused]] Scene* _scene, [[maybe_unused]] const EntityHandle& _owner) {}

void UiDockNode::Finalize() {}

void UiDockNode::Edit([[maybe_unused]] Scene* _scene, [[maybe_unused]] const EntityHandle& _owner, [[maybe_unused]] const std::string& _parentLabel) {
#ifdef _DEBUG
    std::string label = _parentLabel + "##UiDockNode";

    const char* splitNames[] = {"None", "Horizontal", "Vertical"};
    ImGui::Text("Split:%s", splitNames[static_cast<int>(split)]);
    ImGui::DragFloat(("SplitRatio" + label).c_str(), &splitRatio, 0.01f, 0.05f, 0.95f);
    ImGui::DragInt(("ActiveTab" + label).c_str(), &activeTab);
    // childA/childB/splitter/tabBar/contentArea は UiDockBuilder が組み立てた時点で決まる
    // 内部参照なので、parent と同様に編集 UI は作らず、有効/無効だけ読み取り専用で出す。
    ImGui::Text("ChildA:%s ChildB:%s Splitter:%s",
        childA.IsValid() ? "set" : "none",
        childB.IsValid() ? "set" : "none",
        splitter.IsValid() ? "set" : "none");
    ImGui::Text("TabBar:%s ContentArea:%s Windows:%zu",
        tabBar.IsValid() ? "set" : "none",
        contentArea.IsValid() ? "set" : "none",
        windows.size());
    // 実行時の状態は UiDockSystem が書き込むだけなので、ここでは読み取り専用表示にする。
    ImGui::Text("TabsDirty:%d DraggingSplitter:%d",
        static_cast<int>(tabsDirty), static_cast<int>(isDraggingSplitter));
#endif // _DEBUG
}

void to_json(nlohmann::json& _j, const UiDockNode& _c) {
    _j["split"]       = static_cast<uint8_t>(_c.split);
    _j["splitRatio"]  = _c.splitRatio;
    _j["activeTab"]   = _c.activeTab;
    _j["childA"]      = _c.childA;
    _j["childB"]      = _c.childB;
    _j["splitter"]    = _c.splitter;
    _j["tabBar"]      = _c.tabBar;
    _j["contentArea"] = _c.contentArea;
    // windows は実行時の状態として扱う (Stage 5 でレイアウト保存を設計する段になったら
    // そこで改めてスキーマを決める。tabButtons/tabsDirty/isDraggingSplitter/splitterGrabOffset も同様)。
}

void from_json(const nlohmann::json& _j, UiDockNode& _c) {
    if (_j.contains("split"))       { _c.split = static_cast<UiDockSplit>(_j["split"].get<uint8_t>()); }
    if (_j.contains("splitRatio"))  { _j["splitRatio"].get_to(_c.splitRatio); }
    if (_j.contains("activeTab"))   { _j["activeTab"].get_to(_c.activeTab); }
    if (_j.contains("childA"))      { _j["childA"].get_to(_c.childA); }
    if (_j.contains("childB"))      { _j["childB"].get_to(_c.childB); }
    if (_j.contains("splitter"))    { _j["splitter"].get_to(_c.splitter); }
    if (_j.contains("tabBar"))      { _j["tabBar"].get_to(_c.tabBar); }
    if (_j.contains("contentArea")) { _j["contentArea"].get_to(_c.contentArea); }
}

} // namespace LogGuide
