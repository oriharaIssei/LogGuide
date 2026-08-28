#pragma once

#include "entity/EntityHandle.h"
#include "system/ISystem.h"

#include <Vector2.h>
#include <cstdint>

namespace LogGuide {

class UiTransform;
class UiWindow;
class NativeWindowManager;

/// ウィンドウのリサイズ・移動・前面化・閉じる/切り離しボタンの状態遷移を行う.
/// タイトルバーのドラッグでウィンドウを動かし、ウィンドウのどこか（中身のボタン等も含む）を
/// クリックしたら、そのウィンドウを最前面に出す。縁をドラッグすればリサイズできる。
/// 前面化とリサイズはどちらも「全ウィンドウを見てから一番手前のものを選ぶ」必要があるため、
/// UpdateEntity() ではなく Update() をオーバーライドし、entities_（ウィンドウのルート）を
/// 自前で走査する。
class UiWindowSystem final : public OriGine::ISystem {
public:
    UiWindowSystem() : OriGine::ISystem(OriGine::SystemCategory::StateTransition) {}
    ~UiWindowSystem() override = default;

    void Initialize() override;
    void Finalize() override;

    /// 前面化・リサイズは「全ウィンドウを見てから一番手前を選ぶ」必要があるため、
    /// UpdateEntity() ではなく Update() をオーバーライドする.
    void Update() override;

    /// サーフェス (追加の OS ウィンドウ) の問い合わせ先を注入する (v10)。
    /// 未注入 (nullptr) のときは従来通りメインウィンドウのカーソル座標だけを使う。
    void SetSurfaceProvider(NativeWindowManager* _provider) { surfaceProvider_ = _provider; }

    /// 指定したウィンドウを最前面に持ってくる (同じサーフェス内だけで). アプリ側が
    /// 再結合直後のウィンドウを前面化するのに使うための公開ラッパ.
    void BringWindowToFront(const OriGine::EntityHandle& _window) { BringToFront(_window); }

private:
    /// 指定したウィンドウを最前面に持ってきて、同じサーフェスに属するウィンドウの order と
    /// UiTransform::renderPriority を振り直す (v10: 別サーフェスの前後関係は OS が決めるので触らない).
    void BringToFront(const OriGine::EntityHandle& _window);

    /// 指定サーフェス内でカーソルを含むウィンドウのうち、order が最大のもの（一番手前）を 1 つ返す.
    /// 前面化とリサイズ対象の絞り込み（奥のウィンドウの縁を手前のウィンドウ越しに掴めないようにする）の
    /// 両方で使うため、判定ロジックをここに共有する.
    /// 見つからなければ無効なハンドルを返す.
    OriGine::EntityHandle FindFrontMostWindowAt(int32_t _surfaceId, const OriGine::Vec2f& _cursor);

    /// _transform の矩形の内側 kResizeBorder px を左右上下それぞれ判定し、
    /// UiWindow::kEdgeLeft 等の OR を返す（角は 2 ビット立つ）.
    /// _cursor は既に矩形の内側にあること（FindFrontMostWindowAt で確認済みであること）が前提.
    uint32_t ComputeResizeEdges(const UiTransform& _transform, const OriGine::Vec2f& _cursor);

    /// リサイズ中のウィンドウの矩形を、開始時の矩形 + カーソルの移動量から作り直す.
    /// minSize を下回らないようクランプする.
    void ApplyResizeDrag(const UiWindow& _window, UiTransform& _transform, const OriGine::Vec2f& _cursor);

    /// リサイズの縁を示すカーソル形状を engine 側に反映する（_edges == 0 なら解除）.
    void UpdateCursorShape(uint32_t _edges);

    /// ウィンドウ 1 枚が占める優先度の幅. 子はこの範囲内で自由に使える.
    static constexpr int32_t kWindowPriorityBand = 1000;
    /// 縁のリサイズ判定幅 (px).
    static constexpr float kResizeBorder = 6.0f;
    /// タイトルバーをドラッグしてこの距離だけメインウィンドウのクライアント矩形の外に
    /// 出たら切り離し要求を立てる (px).
    static constexpr float kDetachMargin = 24.0f;

    /// サーフェスのサイズ/カーソル座標の問い合わせ先 (v10). 未注入なら従来通りメインウィンドウのみ.
    NativeWindowManager* surfaceProvider_ = nullptr;
};

} // namespace LogGuide
