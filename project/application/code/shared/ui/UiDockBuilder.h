#pragma once

#include "entity/EntityHandle.h"

// UiDockSplit の定義に要る。
#include "ui/component/UiDockNode.h"

#include <Vector2.h>
#include <cstdint>

namespace OriGine {
class Scene;
class SystemRunner;
} // namespace OriGine

namespace LogGuide {

/// タブ 1 つの幅 (px). 文字幅の計測 (TextLayoutSystem) をしてまで可変にするほどの
/// ものではないため固定にする。UiDockSystem からも参照するため公開する.
constexpr float kUiDockTabWidth = 140.0f;
/// タブバーの高さ (px). UiDockSystem からも参照するため公開する.
constexpr float kUiDockTabBarHeight = 28.0f;
/// スプリッターの幅 (px). UiDockSystem からも参照するため公開する.
constexpr float kUiDockSplitterWidth = 6.0f;

/// サーフェスを丸ごと覆うドックスペース (ルートの葉ノード) を作る.
/// _surfaceId は 0 ならメインウィンドウ、1 以上は NativeWindowManager が管理する追加ウィンドウ
/// (UiTransform::surfaceId と同じ意味)。
OriGine::EntityHandle CreateUiDockSpace(OriGine::Scene* _scene, OriGine::SystemRunner* _runner,
                                        int32_t _surfaceId);

/// 葉ノードを分割し、新しくできた側 (_ratio 側。中身の無い空の葉ノード) を返す.
/// _target は葉ノードであること (分割ノードを渡すと何もせず無効なハンドルを返す)。
/// _target 自身は分割ノードに作り替えられ、元々入っていたウィンドウは _ratio 側でない方
/// (_target がそれまで使っていたタブバー/内容領域をそのまま引き継ぐ側) の葉ノードへ移る.
OriGine::EntityHandle SplitUiDockNode(OriGine::Scene* _scene, OriGine::SystemRunner* _runner,
                                      const OriGine::EntityHandle& _target,
                                      UiDockSplit _split, float _ratio);

/// ウィンドウを葉ノードへ入れる (タブとして追加し、追加したタブをアクティブにする).
/// 既に別の葉ノードに入っていた場合は、まずそちらから抜いてから (空になれば畳んでから) 入れ直す.
void DockUiWindow(OriGine::Scene* _scene, const OriGine::EntityHandle& _window,
                  const OriGine::EntityHandle& _leafNode);

/// ウィンドウをドックから外し、フローティングに戻す.
/// 位置とサイズは _position / _size (サーフェスのクライアント座標。左上が原点) にする.
/// 抜けた葉ノードが空になったら、親の分割ノードを畳む (もう一方の子を親の位置へ繰り上げる).
/// 既にフローティングなら何もしない.
void UndockUiWindow(OriGine::Scene* _scene, const OriGine::EntityHandle& _window,
                    const OriGine::Vec2f& _position, const OriGine::Vec2f& _size);

} // namespace LogGuide
