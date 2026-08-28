#pragma once

#include "entity/EntityHandle.h"

#include <Vector2.h>
#include <cstdint>
#include <string>

namespace OriGine {
class Scene;
class SystemRunner;
} // namespace OriGine

namespace LogGuide {

/// タイトルバーの高さ (px). UiWindowBuilder が組み立てるウィンドウ共通の値.
/// v10: 切り離し時に「OS ウィンドウのクライアントサイズ = UI ウィンドウのサイズ - タイトルバー高さ」
/// を計算するのに、アプリ側 (TerminalApp) からも参照する必要があるため公開する.
constexpr float kUiWindowTitleBarHeight = 28.0f;

/// ウィンドウを組み立てた結果.
struct UiWindowHandles {
    OriGine::EntityHandle root{};         ///< ウィンドウ本体
    OriGine::EntityHandle titleBar{};     ///< タイトルバー
    OriGine::EntityHandle contentArea{};  ///< 中身を足すときの親
    OriGine::EntityHandle closeButton{};  ///< 閉じるボタン (タイトルバーの子)
    OriGine::EntityHandle detachButton{}; ///< 切り離しボタン (タイトルバーの子。実際の切り離しは v9/v10)
};

/// 移動できるウィンドウを 1 枚作る.
/// ウィンドウはルート/タイトルバー/内容領域の 3 エンティティで構成される (UiWindow.h 参照)。
/// 手で組むと間違えやすいため、組み立てをこの関数にまとめる。
/// 中身を足すときは、戻り値の contentArea を UiTransform::parent に指定すること.
/// _position / _size は画面左上からの絶対ピクセル.
UiWindowHandles CreateUiWindow(
    OriGine::Scene* _scene,
    OriGine::SystemRunner* _runner,
    const std::string& _title,
    const OriGine::Vec2f& _position,
    const OriGine::Vec2f& _size,
    int32_t _order);

} // namespace LogGuide
