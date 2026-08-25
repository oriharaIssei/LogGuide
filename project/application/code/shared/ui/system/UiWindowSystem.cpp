#include "ui/system/UiWindowSystem.h"

/// engine
// Scene.h は MouseInput を前方宣言しているだけなので、GetPosition() 等を呼ぶには定義が要る。
#include "input/MouseInput.h"
#include "scene/Scene.h"

/// application
#include "ui/component/UiInteractable.h"
#include "ui/component/UiTransform.h"
#include "ui/component/UiWindow.h"

/// stl
#include <algorithm>
#include <cstdint>
#include <utility>
#include <vector>

using namespace OriGine;

namespace LogGuide {

void UiWindowSystem::Initialize() {}

void UiWindowSystem::Finalize() {}

void UiWindowSystem::Update() {
    if (entities_.empty()) {
        return;
    }
    EraseDeadEntity();

    Scene* scene      = GetScene();
    MouseInput* mouse = scene ? scene->GetMouseInput() : nullptr;
    if (!mouse) {
        return;
    }
    const Vec2f cursor = mouse->GetPosition();

    // --- 前面化: カーソルを含むウィンドウのうち一番手前のものを前に出す ---
    // 中身のどの要素がクリックされたかを辿る必要はなく、ウィンドウ矩形の点判定で足りる。
    if (mouse->IsTrigger(MouseButton::LEFT)) {
        EntityHandle frontTarget{};
        int32_t bestOrder = 0;
        bool found        = false;

        for (const auto& entity : entities_) {
            UiTransform* transform = GetComponent<UiTransform>(entity);
            UiWindow* window       = GetComponent<UiWindow>(entity);
            if (!transform || !window || !transform->visible) {
                continue;
            }
            const bool inside =
                cursor[X] >= transform->resolvedMin[X] && cursor[X] < transform->resolvedMax[X] &&
                cursor[Y] >= transform->resolvedMin[Y] && cursor[Y] < transform->resolvedMax[Y];
            if (!inside) {
                continue;
            }
            if (!found || window->order > bestOrder) {
                frontTarget = entity;
                bestOrder   = window->order;
                found       = true;
            }
        }

        if (found) {
            BringToFront(frontTarget);
        }
    }

    // --- 移動: タイトルバーが押されている間、掴んだ位置を保つように動かす ---
    for (const auto& entity : entities_) {
        UiTransform* transform = GetComponent<UiTransform>(entity);
        UiWindow* window       = GetComponent<UiWindow>(entity);
        if (!transform || !window) {
            continue;
        }
        if (!window->movable || !window->titleBar.IsValid()) {
            window->isDragging = false;
            continue;
        }

        UiInteractable* titleInteractable = GetComponent<UiInteractable>(window->titleBar);
        if (!titleInteractable || !titleInteractable->isPressed) {
            window->isDragging = false;
            continue;
        }

        if (!window->isDragging) {
            window->isDragging     = true;
            window->dragGrabOffset = {cursor[X] - transform->resolvedMin[X],
                                       cursor[Y] - transform->resolvedMin[Y]};
        }

        // 点アンカー前提。offsetMin / offsetMax は画面左上からの絶対ピクセル。
        const Vec2f size    = {transform->offsetMax[X] - transform->offsetMin[X],
                                transform->offsetMax[Y] - transform->offsetMin[Y]};
        const Vec2f topLeft = {cursor[X] - window->dragGrabOffset[X],
                                cursor[Y] - window->dragGrabOffset[Y]};

        transform->offsetMin = topLeft;
        transform->offsetMax = {topLeft[X] + size[X], topLeft[Y] + size[Y]};
    }
}

void UiWindowSystem::BringToFront(EntityHandle _window) {
    UiWindow* target = GetComponent<UiWindow>(_window);
    if (!target) {
        return;
    }

    // 既に最前面なら何もしない（毎クリックで振り直すのを避ける）
    bool alreadyFront = true;
    for (const auto& entity : entities_) {
        UiWindow* window = GetComponent<UiWindow>(entity);
        if (window && window != target && window->order > target->order) {
            alreadyFront = false;
            break;
        }
    }
    if (alreadyFront) {
        return;
    }

    // 対象を一番大きい order にしてから、0 から詰め直す。
    int32_t maxOrder = target->order;
    for (const auto& entity : entities_) {
        UiWindow* window = GetComponent<UiWindow>(entity);
        if (window) {
            maxOrder = std::max(maxOrder, window->order);
        }
    }
    target->order = maxOrder + 1;

    // order の昇順に並べ直して 0..n-1 を振り、優先度の帯を割り当てる。
    std::vector<std::pair<int32_t, EntityHandle>> sorted;
    sorted.reserve(entities_.size());
    for (const auto& entity : entities_) {
        UiWindow* window = GetComponent<UiWindow>(entity);
        if (window) {
            sorted.push_back({window->order, entity});
        }
    }
    std::stable_sort(sorted.begin(), sorted.end(),
        [](const auto& _a, const auto& _b) { return _a.first < _b.first; });

    for (size_t i = 0; i < sorted.size(); ++i) {
        UiWindow* window       = GetComponent<UiWindow>(sorted[i].second);
        UiTransform* transform = GetComponent<UiTransform>(sorted[i].second);
        if (!window || !transform) {
            continue;
        }
        window->order            = static_cast<int32_t>(i);
        transform->renderPriority = static_cast<int32_t>(i) * kWindowPriorityBand;
    }
}

} // namespace LogGuide
