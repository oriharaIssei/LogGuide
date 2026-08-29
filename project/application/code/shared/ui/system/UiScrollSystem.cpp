#include "ui/system/UiScrollSystem.h"

/// engine
// Scene.h は MouseInput を前方宣言しているだけなので、GetPosition() 等を呼ぶには定義が要る。
#include "input/MouseInput.h"
#include "scene/Scene.h"

/// application
#include "ui/component/UiInteractable.h"
#include "ui/component/UiScrollView.h"
#include "ui/component/UiTransform.h"
#include "ui/native/NativeWindowManager.h"

/// stl
#include <algorithm>

using namespace OriGine;

namespace LogGuide {

void UiScrollSystem::Initialize() {}

void UiScrollSystem::Finalize() {}

void UiScrollSystem::Update() {
    if (entities_.empty()) {
        return;
    }
    EraseDeadEntity();

    Scene* scene      = GetScene();
    MouseInput* mouse = scene ? scene->GetMouseInput() : nullptr;

    for (const auto& entity : entities_) {
        UiScrollView* scrollView = GetComponent<UiScrollView>(entity);
        UiTransform* viewTransform = GetComponent<UiTransform>(entity);
        if (!scrollView || !viewTransform) {
            continue;
        }

        const int32_t surfaceId = viewTransform->resolvedSurfaceId;

        // 1. ビューポートの高さ (前フレームの UiLayoutSystem の結果。UiWindowSystem 等と同じく
        // StateTransition カテゴリは Movement カテゴリより先に動くため、1 フレーム遅れの値になる)。
        scrollView->viewHeight = viewTransform->resolvedMax[Y] - viewTransform->resolvedMin[Y];
        // 2. 最大スクロール量
        const float maxScroll = std::max(0.0f, scrollView->contentHeight - scrollView->viewHeight);

        // v10: カーソル座標・サーフェス判定は NativeWindowManager が未注入ならメインウィンドウのみ。
        const Vec2f cursor = surfaceProvider_
            ? surfaceProvider_->GetSurfaceCursorPos(surfaceId)
            : (mouse ? mouse->GetPosition() : Vec2f(0.0f, 0.0f));
        const bool underCursor = surfaceProvider_ ? surfaceProvider_->IsSurfaceUnderCursor(surfaceId) : true;

        // 3. ホイール: カーソルがビューポートの内側にあり、かつそのサーフェスがカーソル直下のときだけ反映する。
        const bool cursorInside =
            cursor[X] >= viewTransform->resolvedMin[X] && cursor[X] < viewTransform->resolvedMax[X] &&
            cursor[Y] >= viewTransform->resolvedMin[Y] && cursor[Y] < viewTransform->resolvedMax[Y];
        if (underCursor && cursorInside) {
            // 単位はどちらの経路も「ノッチ数」(1 ノッチ = WHEEL_DELTA) に揃える。
            // NativeWindowManager::GetSurfaceWheelDelta() は追加ウィンドウ分をノッチ数で返すが、
            // メインウィンドウ (surfaceId == 0) は engine の DirectInput 由来の生値 (WHEEL_DELTA 単位)
            // を返すため、ここで揃えて割る (surfaceProvider_ が無いときの直接取得も同様)。
            const int32_t wheelNotches = surfaceProvider_
                ? surfaceProvider_->GetSurfaceWheelDelta(surfaceId)
                : (mouse ? mouse->GetWheelDelta() / WHEEL_DELTA : 0);
            if (wheelNotches != 0) {
                scrollView->scrollOffset -= static_cast<float>(wheelNotches) * scrollView->wheelStep;
            }
        }

        // 4. つまみのドラッグ: UiInteractable::isPressed が立っている間、カーソルの Y の移動を反映する。
        UiInteractable* thumbInteractable = GetComponent<UiInteractable>(scrollView->scrollThumb);
        UiTransform* thumbTransform       = GetComponent<UiTransform>(scrollView->scrollThumb);
        UiTransform* barTransform         = GetComponent<UiTransform>(scrollView->scrollBar);

        // つまみの高さ (最低 24px)。contentHeight が 0 以下 (未設定) ならビューポート全体を占める扱いにする。
        const float thumbHeight = (scrollView->contentHeight > 0.0f)
            ? std::max(24.0f, scrollView->viewHeight * (scrollView->viewHeight / scrollView->contentHeight))
            : scrollView->viewHeight;

        if (thumbInteractable && thumbInteractable->isPressed && thumbTransform && barTransform) {
            if (!scrollView->isDraggingThumb) {
                // ドラッグ開始: 掴んだ位置 (つまみ上端からカーソルまでの距離) を覚えておく
                // (UiWindowSystem::window->dragGrabOffset と同じ考え方)。
                scrollView->isDraggingThumb  = true;
                scrollView->thumbGrabOffset = cursor[Y] - thumbTransform->resolvedMin[Y];
            }
            const float trackHeight    = barTransform->resolvedMax[Y] - barTransform->resolvedMin[Y];
            const float availableTrack = std::max(1.0f, trackHeight - thumbHeight);
            const float desiredTop     = cursor[Y] - scrollView->thumbGrabOffset;
            const float ratio          = std::clamp((desiredTop - barTransform->resolvedMin[Y]) / availableTrack, 0.0f, 1.0f);
            scrollView->scrollOffset   = ratio * maxScroll;
        } else {
            scrollView->isDraggingThumb = false;
        }

        // 5. クランプ
        scrollView->scrollOffset = std::clamp(scrollView->scrollOffset, 0.0f, maxScroll);

        // 6. コンテンツへ反映。横方向 (X) はビルダーが設定したストレッチのままなので触らない。
        if (UiTransform* contentTransform = GetComponent<UiTransform>(scrollView->content)) {
            contentTransform->offsetMin[Y] = -scrollView->scrollOffset;
            contentTransform->offsetMax[Y] = -scrollView->scrollOffset + scrollView->contentHeight;
        }

        // 7. バーとつまみの見た目
        const bool hasScroll = maxScroll > 0.0f;
        if (barTransform) {
            barTransform->visible = hasScroll;
        }
        if (thumbTransform) {
            thumbTransform->visible = hasScroll;
            if (hasScroll && barTransform) {
                const float trackHeight = barTransform->resolvedMax[Y] - barTransform->resolvedMin[Y];
                const float ratio       = maxScroll > 0.0f ? scrollView->scrollOffset / maxScroll : 0.0f;
                const float top         = ratio * std::max(0.0f, trackHeight - thumbHeight);
                thumbTransform->offsetMin[Y] = top;
                thumbTransform->offsetMax[Y] = top + thumbHeight;
            }
        }
    }
}

} // namespace LogGuide
