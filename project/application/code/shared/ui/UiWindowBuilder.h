#pragma once

#include "entity/EntityHandle.h"

// HideUiWindowChrome/ShowUiWindowChrome が UiWindow を引数に取るため定義が要る。
#include "ui/component/UiWindow.h"

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

/// 自作タイトルバー (と閉じる/切り離しボタン) を隠し、内容領域をウィンドウ上端まで広げる.
/// OS ウィンドウへの切り離し (v10) やドック (v14) など、タイトルバーの役割を他の仕組みが
/// 肩代わりするときに使う共通処理. UiTransform::visible は子へ自動伝播しないため、
/// ボタン類も個別に隠す必要がある.
void HideUiWindowChrome(OriGine::Scene* _scene, const UiWindow& _window);

/// HideUiWindowChrome() で隠した表示を元に戻す.
/// closeButton の visible は closable の値に応じて UiWindowSystem が毎フレーム設定し直すため、
/// ここでは戻さない (タイトルバーが見えるようになった次のフレームで正しい状態になる).
void ShowUiWindowChrome(OriGine::Scene* _scene, const UiWindow& _window);

} // namespace LogGuide
