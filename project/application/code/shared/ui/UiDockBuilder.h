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
/// ドロップ先オーバーレイの描画優先度 (v15). 全ウィンドウ (最大でも
/// kWindowPriorityBand * ウィンドウ枚数程度) より必ず手前に出るよう十分大きくしておく.
constexpr int32_t kUiDockOverlayPriority = 5000;

/// ドックスペース (ルート) の描画優先度.
/// ドックは「背景に敷かれた領域」であって、フローティングウィンドウより必ず奥に居る。
/// ところがフローティングウィンドウのルートは UiWindowSystem::BringToFront() が
/// order * kWindowPriorityBand を割り当てるため、起動直後など order が 0 のウィンドウが
/// あると、ドックスペース (既定の 0) と優先度が並んでしまう。
/// 並ぶと UiRenderSystem は「同じ優先度なら矩形が先、テキストが後」で並べ替えるので、
/// ドック側のテキストがフローティングウィンドウの矩形の上に描かれ、
/// 背景が透けて見える (v16 で発覚。ランチャーを一度クリックして order が上がると直る、
/// という分かりにくい症状になっていた)。スプリッター (+4) やタブ (+1) に至っては
/// 同点どころか上回るため、ウィンドウを貫いて描かれていた。
/// ドックツリー全体をまとめて十分低い帯へ落とし、順序を order に依存させない。
constexpr int32_t kUiDockSpacePriority = -1000000;

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

/// ドロップ先を示す半透明の矩形として使い回す 1 枚のエンティティを作る (v15).
/// UiDockSystem がドラッグ中だけ毎フレーム位置/サイズ/visible を書き換える。
/// UiInteractable は付けない (ヒットテストを奪ってしまうため)。サーフェス直下 (親なし) に
/// 置くことで、フローティングウィンドウより必ず手前に描かれるようにする
/// (renderPriority は kUiDockOverlayPriority)。
OriGine::EntityHandle CreateUiDockDropOverlay(OriGine::Scene* _scene, OriGine::SystemRunner* _runner);

} // namespace LogGuide
