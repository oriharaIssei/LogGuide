#pragma once

#include "system/ISystem.h"

/// stl
#include <cstdint>

namespace LogGuide {

/// ウィンドウの移動と前面化を行う.
/// タイトルバーのドラッグでウィンドウを動かし、ウィンドウのどこか（中身のボタン等も含む）を
/// クリックしたら、そのウィンドウを最前面に出す。
/// 前面化は「全ウィンドウを見てから一番手前のものを選ぶ」必要があるため、
/// UpdateEntity() ではなく Update() をオーバーライドし、entities_（ウィンドウのルート）を
/// 自前で 2 パス走査する。
class UiWindowSystem final : public OriGine::ISystem {
public:
    UiWindowSystem() : OriGine::ISystem(OriGine::SystemCategory::StateTransition) {}
    ~UiWindowSystem() override = default;

    void Initialize() override;
    void Finalize() override;

    /// 前面化は「全ウィンドウを見てから一番手前を選ぶ」必要があるため、
    /// UpdateEntity() ではなく Update() をオーバーライドする.
    void Update() override;

private:
    /// 指定したウィンドウを最前面に持ってきて、全ウィンドウの order と
    /// UiTransform::renderPriority を振り直す.
    void BringToFront(OriGine::EntityHandle _window);

    /// ウィンドウ 1 枚が占める優先度の幅. 子はこの範囲内で自由に使える.
    static constexpr int32_t kWindowPriorityBand = 1000;
};

} // namespace LogGuide
