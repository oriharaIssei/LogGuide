#include "ui/UiRegistration.h"

/// ECS
#include "component/ComponentRegistry.h"
#include "system/SystemRegistry.h"

/// engine (v2: テキストのコンポーネントは engine の TextComponent をそのまま使う。
/// 描画は v5 で自作の UiTextRenderSystem に乗り換えたので、TextRenderSystem は登録しない)
#include "component/text/TextComponent.h"

/// application
#include "ui/component/UiDockNode.h"
#include "ui/component/UiHighlight.h"
#include "ui/component/UiInteractable.h"
#include "ui/component/UiRect.h"
#include "ui/component/UiScrollView.h"
#include "ui/component/UiText.h"
#include "ui/component/UiTransform.h"
#include "ui/component/UiWindow.h"
#include "ui/system/UiDockSystem.h"
#include "ui/system/UiHighlightSystem.h"
#include "ui/system/UiInteractionSystem.h"
#include "ui/system/UiLayoutSystem.h"
#include "ui/system/UiRenderSystem.h"
#include "ui/system/UiScrollSystem.h"
#include "ui/system/UiWindowSystem.h"

using namespace OriGine;

namespace LogGuide {

void RegisterUiComponents() {
    ComponentRegistry* componentRegistry = ComponentRegistry::GetInstance();
    componentRegistry->RegisterComponent<UiTransform>();
    componentRegistry->RegisterComponent<UiRect>();
    componentRegistry->RegisterComponent<UiText>();
    componentRegistry->RegisterComponent<UiInteractable>();
    componentRegistry->RegisterComponent<UiHighlight>();
    // v6: ウィンドウの移動/前面化用の状態を持つコンポーネント。
    componentRegistry->RegisterComponent<UiWindow>();
    // v12: 縦スクロールビュー。
    componentRegistry->RegisterComponent<UiScrollView>();
    // v14: ドックツリーとタブ。
    componentRegistry->RegisterComponent<UiDockNode>();
    // engine のコンポーネント。登録しないと AddComponent がヌル参照で落ちる。
    componentRegistry->RegisterComponent<OriGine::TextComponent>();
}

void RegisterUiSystems() {
    SystemRegistry* systemRegistry = SystemRegistry::GetInstance();
    systemRegistry->RegisterSystem<UiLayoutSystem>();
    systemRegistry->RegisterSystem<UiInteractionSystem>();
    systemRegistry->RegisterSystem<UiHighlightSystem>();
    // v7: 矩形とテキストの描画は 1 つの UiRenderSystem にまとめてある。
    // 別システムに分けると SystemRunner::UpdateCategory(Render) が「全部の矩形 → 全部の
    // テキスト」の 2 パスになり、奥のウィンドウのテキストが手前のウィンドウの矩形の上に
    // 出てしまうため (二重描画を避けるため engine の TextRenderSystem は登録しない)。
    systemRegistry->RegisterSystem<UiRenderSystem>();
    // v6: ウィンドウの移動/前面化。
    systemRegistry->RegisterSystem<UiWindowSystem>();
    // v12: 縦スクロールビューのホイール/つまみドラッグ。
    systemRegistry->RegisterSystem<UiScrollSystem>();
    // v14: ドックツリーとタブ。SystemRunner::RegisterSystem() で実際にシーンへ登録する際、
    // UiWindowSystem よりカーソル形状の決定が後になるよう priority を大きくすること
    // (UiDockSystem.h のコメント参照。ここは型を SystemRegistry に登録するだけで priority は無関係)。
    systemRegistry->RegisterSystem<UiDockSystem>();
}

} // namespace LogGuide
