#pragma once

#include "system/ISystem.h"

namespace LogGuide {

class NativeWindowManager;

/// UiTransform + UiInteractable を持つエンティティのヒットテストと押下状態の更新を行う.
/// 「全要素を見てから最前面を決める」2 パスが要るため、
/// UpdateEntity() ではなく Update() をオーバーライドする.
class UiInteractionSystem final : public OriGine::ISystem {
public:
    UiInteractionSystem() : OriGine::ISystem(OriGine::SystemCategory::Input) {}
    ~UiInteractionSystem() override = default;

    void Initialize() override;
    void Finalize() override;

    void Update() override;

    /// サーフェス (追加の OS ウィンドウ) の問い合わせ先を注入する (v10)。
    /// 未注入 (nullptr) のときは従来通りメインウィンドウのカーソル座標だけを使う。
    void SetSurfaceProvider(NativeWindowManager* _provider) { surfaceProvider_ = _provider; }

private:
    NativeWindowManager* surfaceProvider_ = nullptr;
};

} // namespace LogGuide
