#include "ui/system/UiInteractionSystem.h"

/// engine
// Scene.h は MouseInput を前方宣言しているだけなので、GetPosition() 等を呼ぶには定義が要る。
#include "input/MouseInput.h"
#include "scene/Scene.h"

/// application
#include "ui/component/UiInteractable.h"
#include "ui/component/UiTransform.h"
#include "ui/native/NativeWindowManager.h"

/// stl
#include <cstdint>
#include <unordered_map>

using namespace OriGine;

namespace LogGuide {

void UiInteractionSystem::Initialize() {}

void UiInteractionSystem::Finalize() {}

void UiInteractionSystem::Update() {
    if (entities_.empty()) {
        return;
    }
    EraseDeadEntity();

    Scene* scene      = GetScene();
    MouseInput* mouse = scene ? scene->GetMouseInput() : nullptr;

    // --- パス 1: 状態を一旦落としつつ、サーフェスごとにカーソル下の最前面を探す ---
    // 「最前面を 1 つ選ぶ」比較はサーフェスをまたいでは意味が無いので、サーフェス ID ごとに行う。
    struct HoverCandidate {
        UiInteractable* target = nullptr;
        int32_t priority       = 0;
    };
    std::unordered_map<int32_t, HoverCandidate> hoverBySurface;

    for (const auto& entity : entities_) {
        UiTransform* transform       = GetComponent<UiTransform>(entity);
        UiInteractable* interactable = GetComponent<UiInteractable>(entity);
        if (!transform || !interactable) {
            continue;
        }

        // クリックは 1 フレームだけ立てるので、毎フレーム先頭で落とす
        interactable->wasClicked = false;
        interactable->isHovered  = false;

        if (!interactable->enabled || !transform->resolvedVisible || !mouse) {
            interactable->isPressed = false;
            continue;
        }

        const int32_t surfaceId = transform->resolvedSurfaceId;
        // このサーフェスがカーソルの真下に無ければ、ホバーの候補から外す
        // (重なった OS ウィンドウ越しに裏のウィンドウのボタンが反応しないようにするため)。
        // isPressed には触れない: ドラッグ中にカーソルがサーフェスの外へ出ても、
        // 押下状態そのものはボタンが離されるまで保つ必要がある (切り離しドラッグの途中で
        // カーソルがメインウィンドウの外に出るケースが該当する)。
        const bool underCursor = surfaceProvider_ ? surfaceProvider_->IsSurfaceUnderCursor(surfaceId) : true;
        if (!underCursor) {
            continue;
        }

        const Vec2f cursor = surfaceProvider_ ? surfaceProvider_->GetSurfaceCursorPos(surfaceId) : mouse->GetPosition();

        const bool inside =
            cursor[X] >= transform->resolvedMin[X] && cursor[X] < transform->resolvedMax[X] &&
            cursor[Y] >= transform->resolvedMin[Y] && cursor[Y] < transform->resolvedMax[Y];
        if (!inside) {
            continue;
        }

        // 手前にあるものを優先する。同じ priority のときは後に見た方を採用する
        // （entities_ の順序は安定なので結果は毎フレーム同じになる）
        // ウィンドウが前面化するとその子孫もまとめて前後するよう、階層で加算済みの
        // resolvedPriority で比較する。
        HoverCandidate& best = hoverBySurface[surfaceId];
        if (best.target == nullptr || transform->resolvedPriority >= best.priority) {
            best.target   = interactable;
            best.priority = transform->resolvedPriority;
        }
    }

    for (auto& [surfaceId, candidate] : hoverBySurface) {
        if (candidate.target != nullptr) {
            candidate.target->isHovered = true;
        }
    }

    if (!mouse) {
        return;
    }

    // --- パス 2: 押し下げとクリックの成立 ---
    // v11: engine の MouseInput は DISCL_FOREGROUND でメインウィンドウに結び付いているため、
    // 切り離した OS ウィンドウにフォーカスがあるとボタンが取れない。surfaceProvider_ が
    // 注入されていれば UI 自前のマウス状態 (NativeWindowManager::UpdateMouseState) を使う。
    const bool triggered = surfaceProvider_ ? surfaceProvider_->IsMouseTrigger() : mouse->IsTrigger(MouseButton::LEFT);
    const bool released  = surfaceProvider_ ? surfaceProvider_->IsMouseRelease() : mouse->IsRelease(MouseButton::LEFT);
    if (!triggered && !released) {
        return;
    }

    for (const auto& entity : entities_) {
        UiInteractable* interactable = GetComponent<UiInteractable>(entity);
        if (!interactable || !interactable->enabled) {
            continue;
        }

        if (triggered && interactable->isHovered) {
            interactable->isPressed = true;
        }
        if (released) {
            // 押した要素の上で離されたときだけクリックとみなす
            if (interactable->isPressed && interactable->isHovered) {
                interactable->wasClicked = true;
            }
            interactable->isPressed = false;
        }
    }
}

} // namespace LogGuide
