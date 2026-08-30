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
#include <vector>

namespace LogGuide {

class NativeWindowManager;

/// v15: ドラッグ中のウィンドウが、葉ノードのどこに落ちようとしているか.
enum class UiDockDropZone : uint8_t {
    None = 0, ///< どの葉ノードの上にも無い (何もしない)
    Center,   ///< タブとして追加する
    Left,     ///< 左へ新しく分割する
    Right,    ///< 右へ新しく分割する
    Top,      ///< 上へ新しく分割する
    Bottom,   ///< 下へ新しく分割する
};

/// v15: ドラッグ中のドロップ先 (一時的な状態なので UiDockNode ではなく UiDockSystem が持つ)。
struct UiDockDropTarget {
    OriGine::EntityHandle leaf{};
    UiDockDropZone zone = UiDockDropZone::None;
};

/// v15: ドロップでウィンドウをドックする要求.
/// UiDockSystem は積むだけで、実際の DockUiWindow / SplitUiDockNode の呼び出しはアプリ
/// (TerminalApp) が行う (v10 の切り離しと同じ「システムは要求、生成はアプリ」の分担)。
struct UiDockRequest {
    OriGine::EntityHandle window{};
    OriGine::EntityHandle leaf{};
    UiDockDropZone zone = UiDockDropZone::None;
};

/// v15: タブを引き剥がしてフローティングへ戻す要求.
/// 実際の UndockUiWindow の呼び出しと isDragging/dragGrabOffset の設定はアプリが行う
/// (同上の分担)。
struct UiDockTearOffRequest {
    OriGine::EntityHandle window{};
    /// アンドック後の位置 (サーフェスのクライアント座標、左上原点)。
    OriGine::Vec2f position{};
    /// タイトルバーのドラッグに引き継ぐための掴み位置 (UiWindow::dragGrabOffset にそのまま入れる)。
    OriGine::Vec2f grabOffset{};
};

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

    /// v15: ドロップ先を示す半透明矩形として使い回すエンティティを注入する
    /// (アプリが CreateUiDockDropOverlay() で 1 つだけ作って渡す)。
    void SetDropOverlay(const OriGine::EntityHandle& _overlay) { dropOverlay_ = _overlay; }

    /// v15: 現在のドロップ先 (デバッグ表示用に読めるようにしておく)。
    const UiDockDropTarget& GetDropTarget() const { return dropTarget_; }

    /// v15: 積まれたドック要求を取り出す (呼ぶと空になる)。TerminalApp が実際の
    /// DockUiWindow / SplitUiDockNode の呼び出しを行う。
    std::vector<UiDockRequest> TakeDockRequests();
    /// v15: 積まれたタブ引き剥がし要求を取り出す (呼ぶと空になる)。TerminalApp が実際の
    /// UndockUiWindow の呼び出しと isDragging/dragGrabOffset の設定を行う。
    std::vector<UiDockTearOffRequest> TakeTearOffRequests();

private:
    /// 分割ノード 1 つ分の更新 (アンカー決定 + スプリッターのドラッグ/カーソル形状)。
    void UpdateSplitNode(UiDockNode& _node, UiTransform& _transform, bool _released);
    /// 葉ノード 1 つ分の更新 (タブの作り直し + タブ切り替え + タブの引き剥がし判定 +
    /// windows の表示状態)。
    void UpdateLeafNode(const OriGine::EntityHandle& _leaf, UiDockNode& _node);
    /// windows の並びからタブボタンを作り直す (前回分は必ず破棄してから作り直す。v13 と同じ作法)。
    void RebuildTabButtons(UiDockNode& _node);

    /// v15: タブボタンを押したまま kTearOffDistance 動いたら引き剥がし要求を積む。
    void HandleTabTearOff(const OriGine::EntityHandle& _leaf, UiDockNode& _node);
    /// v15: ドラッグ中のフローティングウィンドウを探し、ドロップ先を判定してオーバーレイを
    /// 更新する。離されていればドック要求を積む。
    /// v16: 判定はカーソル 1 点で行う。タブバーの上ならタブ結合、内容領域の縁ならその辺へ分割。
    /// 一度「ドラッグ中のウィンドウのタイトルバー矩形との重なり面積」で判定したが、
    /// タイトルバーは幅が数百 px あるため、カーソルが別の場所にあっても端がかすっただけで
    /// 判定が出てしまい、縁の判定が大きすぎる操作感になったため戻した。
    void UpdateDropTarget(bool _released);

    /// 指定サーフェスのクライアント座標でのカーソル位置。未注入ならメインウィンドウのみ。
    OriGine::Vec2f SurfaceCursor(int32_t _surfaceId);
    /// 指定サーフェスがカーソル直下にあるか。未注入なら常に true。
    bool IsSurfaceUnderCursor(int32_t _surfaceId);
    /// スプリッターのカーソル形状 (左右矢印 or 上下矢印) を engine 側に反映する。
    void ClaimSplitterCursor(bool _isHorizontal);

    /// タブを押したまま、引き剥がしと判定するまでの距離 (px)。
    static constexpr float kTearOffDistance = 12.0f;
    /// v16: 分割の判定に使う帯の幅 (px)。葉ノードの内容領域の縁からこの距離までに
    /// カーソルが入ったら、その辺へ分割する。ここより内側は「中央」= ドロップ先なし。
    static constexpr float kUiDockSplitBand = 32.0f;

    /// サーフェスのサイズ/カーソル座標の問い合わせ先 (v10). 未注入なら従来通りメインウィンドウのみ.
    NativeWindowManager* surfaceProvider_ = nullptr;

    /// v15: ドロップ先オーバーレイのエンティティ (アプリが注入する。1 つを使い回す)。
    OriGine::EntityHandle dropOverlay_{};
    /// v15: 現フレームのドロップ先判定結果。
    UiDockDropTarget dropTarget_{};
    /// v15: ドラッグ中と認識して追跡しているウィンドウ。
    /// release されたフレームは UiWindowSystem が titleBar の isPressed 解除を見て
    /// 先に UiWindow::isDragging を false に戻してしまっている (StateTransition 内で
    /// UiWindowSystem の方が先に実行されるため) ので、直前フレームまで追跡していた
    /// このハンドルを release 判定にも使う。
    OriGine::EntityHandle activeDragWindow_{};
    /// v15: 積まれたドック要求 (TakeDockRequests() で取り出す)。
    std::vector<UiDockRequest> dockRequests_;
    /// v15: 積まれたタブ引き剥がし要求 (TakeTearOffRequests() で取り出す)。
    std::vector<UiDockTearOffRequest> tearOffRequests_;

    /// v15: タブの引き剥がし判定用に、現在追跡しているタブ押下の状態。
    struct TabTearState {
        OriGine::EntityHandle leaf{};
        int32_t tabIndex = -1;
        OriGine::Vec2f pressCursor{};
        bool torn = false; ///< 既に引き剥がし要求を積んだか (同じ押下中は積み直さない)。
    };
    TabTearState tabTear_{};
};

} // namespace LogGuide
