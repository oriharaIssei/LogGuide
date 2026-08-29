#pragma once

#include "system/ISystem.h"

// UpdateSplitNode/UpdateLeafNode が UiDockNode&/UiTransform& を引数に取る。
// ポインタ/参照の宣言だけなら前方宣言でも足りるはずだが、過去に前方宣言止まりで
// ビルドが落ちた例がある (UiLayoutSystem.h 参照)ため、定義のあるヘッダを直接 include して
// 自己完結させておく。
#include "ui/component/UiDockNode.h"
#include "ui/component/UiTransform.h"

#include <Vector2.h>
#include <cstdint>

namespace LogGuide {

class NativeWindowManager;

/// ImGui のドッキングに相当する「ドックツリーとタブ」の機構 (v14)。
/// ツリー構造は UiTransform::parent で表し、矩形の計算とクリップは UiLayoutSystem に任せる
/// (ここではドック専用のレイアウト計算を書かない)。分割ノードのときは childA/childB/splitter の
/// アンカーを splitRatio から毎フレーム決め直し、葉ノードのときはタブの作り直しと
/// windows の表示切り替えを行う (詳細は UiDockNode.h 参照)。
///
/// カーソル形状の競合について:
/// UiWindowSystem もリサイズ縁のカーソル形状に WinApp::SetCursorShapeOverride() を使っており、
/// 両方が毎フレーム無条件に書き込むと「後から実行された方が勝つ」(ホバーが無いときに nullptr で
/// 解除する分もそのまま勝ってしまう) ため、何もしていない側が正しい側の結果を消してしまう。
/// ここでは「スプリッターが実際にホバー/ドラッグされているときだけ」呼ぶことで、
/// 何も主張しないフレームは UiWindowSystem 側の結果 (nullptr での解除を含む) をそのまま残す。
/// この仕組みが機能するには、登録側 (TerminalApp) で UiDockSystem を UiWindowSystem より
/// 後に実行されるよう priority を大きくして登録する必要がある
/// (同じ StateTransition カテゴリ内の実行順は SystemRunner が priority 昇順で決めるため)。
class UiDockSystem final : public OriGine::ISystem {
public:
    UiDockSystem() : OriGine::ISystem(OriGine::SystemCategory::StateTransition) {}
    ~UiDockSystem() override = default;

    void Initialize() override;
    void Finalize() override;
    void Update() override;

    /// サーフェス (追加の OS ウィンドウ) の問い合わせ先を注入する (v10 の他システムと同じ流儀)。
    /// 未注入 (nullptr) のときは従来通りメインウィンドウのカーソル座標だけを使う。
    void SetSurfaceProvider(NativeWindowManager* _provider) { surfaceProvider_ = _provider; }

private:
    /// 分割ノード 1 つ分の更新 (アンカー決定 + スプリッターのドラッグ/カーソル形状)。
    void UpdateSplitNode(UiDockNode& _node, UiTransform& _transform, bool _released);
    /// 葉ノード 1 つ分の更新 (タブの作り直し + タブ切り替え + windows の表示状態)。
    void UpdateLeafNode(UiDockNode& _node);
    /// windows の並びからタブボタンを作り直す (前回分は必ず破棄してから作り直す。v13 と同じ作法)。
    void RebuildTabButtons(UiDockNode& _node);

    /// 指定サーフェスのクライアント座標でのカーソル位置。未注入ならメインウィンドウのみ。
    OriGine::Vec2f SurfaceCursor(int32_t _surfaceId);
    /// 指定サーフェスがカーソル直下にあるか。未注入なら常に true。
    bool IsSurfaceUnderCursor(int32_t _surfaceId);
    /// スプリッターのカーソル形状 (左右矢印 or 上下矢印) を engine 側に反映する。
    void ClaimSplitterCursor(bool _isHorizontal);

    /// サーフェスのサイズ/カーソル座標の問い合わせ先 (v10). 未注入なら従来通りメインウィンドウのみ.
    NativeWindowManager* surfaceProvider_ = nullptr;
};

} // namespace LogGuide
