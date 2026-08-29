#pragma once

#include "system/ISystem.h"

namespace LogGuide {

class NativeWindowManager;

/// UiScrollView を持つビューポートのホイール入力とつまみのドラッグを処理し、
/// コンテンツの位置とスクロールバー/つまみの見た目を毎フレーム更新する.
/// 「ビューポートの矩形」と「つまみの UiInteractable」を両方見る必要があるが、1 エンティティ内で
/// 完結する処理なので UpdateEntity() で書けるものの、UiWindowSystem / UiHighlightSystem と同じく
/// StateTransition カテゴリで動く他システムとの並びを揃えるため Update() をオーバーライドする.
class UiScrollSystem final : public OriGine::ISystem {
public:
    UiScrollSystem() : OriGine::ISystem(OriGine::SystemCategory::StateTransition) {}
    ~UiScrollSystem() override = default;

    void Initialize() override;
    void Finalize() override;

    void Update() override;

    /// サーフェス (追加の OS ウィンドウ) の問い合わせ先を注入する.
    /// 未注入 (nullptr) のときは従来通りメインウィンドウのカーソル座標/ホイールだけを使う.
    void SetSurfaceProvider(NativeWindowManager* _provider) { surfaceProvider_ = _provider; }

private:
    /// サーフェス (追加の OS ウィンドウ) の問い合わせ先 (v12). 未注入なら従来通りメインウィンドウのみ.
    NativeWindowManager* surfaceProvider_ = nullptr;
};

} // namespace LogGuide
