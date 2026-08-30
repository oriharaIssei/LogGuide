#include "ui/component/UiWindow.h"

#ifdef _DEBUG
#include <imgui/imgui.h>
#endif // _DEBUG

using namespace OriGine;

namespace LogGuide {

void UiWindow::Initialize([[maybe_unused]] Scene* _scene, [[maybe_unused]] const EntityHandle& _owner) {}

void UiWindow::Finalize() {}

void UiWindow::Edit([[maybe_unused]] Scene* _scene, [[maybe_unused]] const EntityHandle& _owner, [[maybe_unused]] const std::string& _parentLabel) {
#ifdef _DEBUG
    std::string label = _parentLabel + "##UiWindow";

    ImGui::Checkbox(("Movable" + label).c_str(), &movable);
    ImGui::DragInt(("Order" + label).c_str(), &order);
    ImGui::Checkbox(("Closable" + label).c_str(), &closable);
    ImGui::Checkbox(("Resizable" + label).c_str(), &resizable);
    ImGui::DragFloat2(("MinSize" + label).c_str(), &minSize[X], 1.0f, 1.0f, 4096.0f);
    // titleBar / contentArea / closeButton / detachButton は UiWindowBuilder が組み立てた時点で
    // 決まる内部参照なので、parent と同様に編集 UI は作らず、有効/無効だけ読み取り専用で出す。
    ImGui::Text("TitleBar:%s ContentArea:%s CloseButton:%s DetachButton:%s",
        titleBar.IsValid() ? "set" : "none",
        contentArea.IsValid() ? "set" : "none",
        closeButton.IsValid() ? "set" : "none",
        detachButton.IsValid() ? "set" : "none");
    // 実行時の状態は UiWindowSystem が書き込むだけなので、ここでは読み取り専用表示にする。
    ImGui::Text("Dragging:%d GrabOffset:(%.1f, %.1f)",
        static_cast<int>(isDragging), dragGrabOffset[X], dragGrabOffset[Y]);
    ImGui::Text("Resizing:%d Edges:0x%X", static_cast<int>(isResizing), resizeEdges);
    ImGui::Text("CloseRequested:%d DetachRequested:%d",
        static_cast<int>(closeRequested), static_cast<int>(detachRequested));
    // v14: dockNode も実行時の状態 (UiDockBuilder が書き込む) なので読み取り専用表示にする。
    ImGui::Text("DockNode:%s", dockNode.IsValid() ? "set" : "none");
    // v15: floatingSize も UiDockBuilder が書き込む実行時の状態。
    ImGui::Text("FloatingSize:(%.1f, %.1f)", floatingSize[X], floatingSize[Y]);
#endif // _DEBUG
}

void to_json(nlohmann::json& _j, const UiWindow& _c) {
    _j["movable"]     = _c.movable;
    _j["order"]       = _c.order;
    _j["titleBar"]    = _c.titleBar;
    _j["contentArea"] = _c.contentArea;
    // closable / resizable / minSize は設定値なので保存する。
    // closeButton / detachButton (エンティティ参照) と実行時の状態は保存しない。
    _j["closable"]  = _c.closable;
    _j["resizable"] = _c.resizable;
    _j["minSize"]   = {_c.minSize[X], _c.minSize[Y]};
}

void from_json(const nlohmann::json& _j, UiWindow& _c) {
    if (_j.contains("movable"))     { _j["movable"].get_to(_c.movable); }
    if (_j.contains("order"))       { _j["order"].get_to(_c.order); }
    if (_j.contains("titleBar"))    { _j["titleBar"].get_to(_c.titleBar); }
    if (_j.contains("contentArea")) { _j["contentArea"].get_to(_c.contentArea); }
    if (_j.contains("closable"))    { _j["closable"].get_to(_c.closable); }
    if (_j.contains("resizable"))   { _j["resizable"].get_to(_c.resizable); }
    if (_j.contains("minSize"))     { _j["minSize"].get_to(_c.minSize); }
}

} // namespace LogGuide
