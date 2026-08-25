#include "ui/system/UiInteractionSystem.h"

/// engine
// Scene.h は MouseInput を前方宣言しているだけなので、GetPosition() 等を呼ぶには定義が要る。
#include "input/MouseInput.h"
#include "scene/Scene.h"

/// application
#include "ui/component/UiInteractable.h"
#include "ui/component/UiTransform.h"

/// stl
#include <cstdint>

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

    // --- パス 1: 状態を一旦落としつつ、カーソル下の最前面を探す ---
    UiInteractable* hoverTarget = nullptr;
    int32_t bestPriority        = 0;

    const Vec2f cursor = mouse ? mouse->GetPosition() : Vec2f{-1.0f, -1.0f};

    for (const auto& entity : entities_) {
        UiTransform* transform       = GetComponent<UiTransform>(entity);
        UiInteractable* interactable = GetComponent<UiInteractable>(entity);
        if (!transform || !interactable) {
            continue;
        }

        // クリックは 1 フレームだけ立てるので、毎フレーム先頭で落とす
        interactable->wasClicked = false;
        interactable->isHovered  = false;

        if (!interactable->enabled || !transform->visible || !mouse) {
            interactable->isPressed = false;
            continue;
        }

        const bool inside =
            cursor[X] >= transform->resolvedMin[X] && cursor[X] < transform->resolvedMax[X] &&
            cursor[Y] >= transform->resolvedMin[Y] && cursor[Y] < transform->resolvedMax[Y];

        // 手前にあるものを優先する。同じ priority のときは後に見た方を採用する
        // （entities_ の順序は安定なので結果は毎フレーム同じになる）
        if (inside && (hoverTarget == nullptr || transform->renderPriority >= bestPriority)) {
            hoverTarget  = interactable;
            bestPriority = transform->renderPriority;
        }
    }

    if (hoverTarget != nullptr) {
        hoverTarget->isHovered = true;
    }

    if (!mouse) {
        return;
    }

    // --- パス 2: 押し下げとクリックの成立 ---
    const bool triggered = mouse->IsTrigger(MouseButton::LEFT);
    const bool released  = mouse->IsRelease(MouseButton::LEFT);
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
