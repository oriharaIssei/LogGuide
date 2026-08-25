#include "ui/component/UiWindow.h"

#ifdef _DEBUG
#include <imgui/imgui.h>
#endif // _DEBUG

using namespace OriGine;

namespace LogGuide {

void UiWindow::Initialize([[maybe_unused]] Scene* _scene, [[maybe_unused]] EntityHandle _owner) {}

void UiWindow::Finalize() {}

void UiWindow::Edit([[maybe_unused]] Scene* _scene, [[maybe_unused]] EntityHandle _owner, [[maybe_unused]] const std::string& _parentLabel) {
#ifdef _DEBUG
    std::string label = _parentLabel + "##UiWindow";

    ImGui::Checkbox(("Movable" + label).c_str(), &movable);
    ImGui::DragInt(("Order" + label).c_str(), &order);
    // titleBar / contentArea は UiWindowBuilder が組み立てた時点で決まる内部参照なので、
    // parent と同様に編集 UI は作らず、有効/無効だけ読み取り専用で出す。
    ImGui::Text("TitleBar:%s ContentArea:%s",
        titleBar.IsValid() ? "set" : "none",
        contentArea.IsValid() ? "set" : "none");
    // 実行時の状態は UiWindowSystem が書き込むだけなので、ここでは読み取り専用表示にする。
    ImGui::Text("Dragging:%d GrabOffset:(%.1f, %.1f)",
        static_cast<int>(isDragging), dragGrabOffset[X], dragGrabOffset[Y]);
#endif // _DEBUG
}

void to_json(nlohmann::json& _j, const UiWindow& _c) {
    _j["movable"]     = _c.movable;
    _j["order"]       = _c.order;
    _j["titleBar"]    = _c.titleBar;
    _j["contentArea"] = _c.contentArea;
}

void from_json(const nlohmann::json& _j, UiWindow& _c) {
    if (_j.contains("movable"))     { _j["movable"].get_to(_c.movable); }
    if (_j.contains("order"))       { _j["order"].get_to(_c.order); }
    if (_j.contains("titleBar"))    { _j["titleBar"].get_to(_c.titleBar); }
    if (_j.contains("contentArea")) { _j["contentArea"].get_to(_c.contentArea); }
}

} // namespace LogGuide
