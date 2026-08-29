#include "ui/component/UiScrollView.h"

#ifdef _DEBUG
#include <imgui/imgui.h>
#endif // _DEBUG

using namespace OriGine;

namespace LogGuide {

void UiScrollView::Initialize([[maybe_unused]] Scene* _scene, [[maybe_unused]] const EntityHandle& _owner) {}

void UiScrollView::Finalize() {}

void UiScrollView::Edit([[maybe_unused]] Scene* _scene, [[maybe_unused]] const EntityHandle& _owner, [[maybe_unused]] const std::string& _parentLabel) {
#ifdef _DEBUG
    std::string label = _parentLabel + "##UiScrollView";

    ImGui::DragFloat(("WheelStep" + label).c_str(), &wheelStep, 1.0f, 0.0f, 512.0f);
    ImGui::DragFloat(("ScrollBarWidth" + label).c_str(), &scrollBarWidth, 0.5f, 0.0f, 64.0f);
    // content / scrollBar / scrollThumb は UiWidgetBuilder が組み立てた時点で決まる内部参照なので、
    // parent と同様に編集 UI は作らず、有効/無効だけ読み取り専用で出す。
    ImGui::Text("Content:%s ScrollBar:%s ScrollThumb:%s",
        content.IsValid() ? "set" : "none",
        scrollBar.IsValid() ? "set" : "none",
        scrollThumb.IsValid() ? "set" : "none");
    // 実行時の状態は UiScrollSystem が書き込むだけなので、ここでは読み取り専用表示にする。
    ImGui::Text("Offset:%.1f ContentHeight:%.1f ViewHeight:%.1f Dragging:%d",
        scrollOffset, contentHeight, viewHeight, static_cast<int>(isDraggingThumb));
#endif // _DEBUG
}

void to_json(nlohmann::json& _j, const UiScrollView& _c) {
    _j["wheelStep"]      = _c.wheelStep;
    _j["scrollBarWidth"] = _c.scrollBarWidth;
    // content は外部 (アプリ) が参照する内部参照なので保存する。scrollBar / scrollThumb は
    // UiWindow の closeButton / detachButton と同様、ビルダーが組み立て直せる内部専用の参照なので
    // 保存しない。
    _j["content"] = _c.content;
}

void from_json(const nlohmann::json& _j, UiScrollView& _c) {
    if (_j.contains("wheelStep"))      { _j["wheelStep"].get_to(_c.wheelStep); }
    if (_j.contains("scrollBarWidth")) { _j["scrollBarWidth"].get_to(_c.scrollBarWidth); }
    if (_j.contains("content"))        { _j["content"].get_to(_c.content); }
}

} // namespace LogGuide
