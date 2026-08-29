#include "ui/system/UiWindowSystem.h"

/// engine
#include "Engine.h"
// Scene.h は MouseInput を前方宣言しているだけなので、GetPosition() 等を呼ぶには定義が要る。
#include "input/MouseInput.h"
#include "scene/Scene.h"
#include "winApp/WinApp.h"

/// application
#include "ui/component/UiInteractable.h"
#include "ui/component/UiTransform.h"
#include "ui/component/UiWindow.h"
#include "ui/native/NativeWindowManager.h"

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
    // v11: engine の MouseInput は DISCL_FOREGROUND でメインウィンドウに結び付いているため、
    // 切り離した OS ウィンドウにフォーカスがあるとボタンが取れない。surfaceProvider_ が
    // 注入されていれば UI 自前のマウス状態 (NativeWindowManager::UpdateMouseState) を使う。
    const bool triggered = surfaceProvider_ ? surfaceProvider_->IsMouseTrigger() : mouse->IsTrigger(MouseButton::LEFT);
    const bool released  = surfaceProvider_ ? surfaceProvider_->IsMouseRelease() : mouse->IsRelease(MouseButton::LEFT);

    // v10: カーソル座標はエンティティが乗っているサーフェスに応じて取る。
    // NativeWindowManager が未注入なら、従来通りメインウィンドウのカーソル座標だけを使う
    // (GetSurfaceCursorPos(0) と同じ結果になるので、以降は分岐せずこのヘルパで統一する)。
    auto surfaceCursor = [this, mouse](int32_t _surfaceId) -> Vec2f {
        return surfaceProvider_ ? surfaceProvider_->GetSurfaceCursorPos(_surfaceId) : mouse->GetPosition();
    };
    auto isUnderCursor = [this](int32_t _surfaceId) -> bool {
        return surfaceProvider_ ? surfaceProvider_->IsSurfaceUnderCursor(_surfaceId) : true;
    };

    // このフレームに存在するウィンドウが使っているサーフェス ID の一覧 (重複無し)。
    // 前面化・リサイズ開始・カーソル形状の判定は、カーソルの真下にあるサーフェスに対してだけ行う。
    std::vector<int32_t> activeSurfaces;
    for (const auto& entity : entities_) {
        if (UiTransform* transform = GetComponent<UiTransform>(entity)) {
            if (std::find(activeSurfaces.begin(), activeSurfaces.end(), transform->resolvedSurfaceId) == activeSurfaces.end()) {
                activeSurfaces.push_back(transform->resolvedSurfaceId);
            }
        }
    }

    // 処理順は リサイズ判定 → 前面化 → 移動。
    // リサイズを先に判定しないと、縁を掴んだつもりがタイトルバーのドラッグ移動になってしまう。

    // --- リサイズ ---
    // まず既にリサイズ中のウィンドウが無いか探す（同時に複数枚がリサイズ中になることは無い想定）。
    EntityHandle resizingEntity{};
    for (const auto& entity : entities_) {
        UiWindow* window = GetComponent<UiWindow>(entity);
        if (window && window->isResizing) {
            resizingEntity = entity;
            break;
        }
    }
    bool isResizingThisFrame = resizingEntity.IsValid();

    if (isResizingThisFrame) {
        UiWindow* window       = GetComponent<UiWindow>(resizingEntity);
        UiTransform* transform = GetComponent<UiTransform>(resizingEntity);
        if (window && transform) {
            // リサイズ継続中はサーフェスの外にカーソルが多少出ても構わないので under-cursor 判定はしない。
            const Vec2f cursor = surfaceCursor(transform->resolvedSurfaceId);
            ApplyResizeDrag(*window, *transform, cursor);
            if (released) {
                window->isResizing = false;
                isResizingThisFrame = false;
            }
        }
    } else if (triggered) {
        // カーソルの真下にあるサーフェスの中で、一番手前のウィンドウ「だけ」を縁の判定対象にする。
        // こうしないと奥のウィンドウの縁が手前のウィンドウ越しに掴めてしまうし、別サーフェスの
        // ウィンドウ越しに他方の縁が反応することも無くなる。
        for (int32_t surfaceId : activeSurfaces) {
            if (!isUnderCursor(surfaceId)) {
                continue;
            }
            const Vec2f cursor     = surfaceCursor(surfaceId);
            EntityHandle topWindow = FindFrontMostWindowAt(surfaceId, cursor);
            if (topWindow.IsValid()) {
                UiWindow* window       = GetComponent<UiWindow>(topWindow);
                UiTransform* transform = GetComponent<UiTransform>(topWindow);
                if (window && transform && window->resizable) {
                    const uint32_t edges = ComputeResizeEdges(*transform, cursor);
                    if (edges != 0) {
                        window->isResizing        = true;
                        window->resizeEdges       = edges;
                        window->resizeStartCursor = cursor;
                        window->resizeStartMin    = transform->offsetMin;
                        window->resizeStartMax    = transform->offsetMax;
                        resizingEntity            = topWindow;
                        isResizingThisFrame       = true;
                    }
                }
            }
            // カーソルの真下にあるサーフェスは高々 1 つのはず (OS 的に重なった 2 枚を同時には
            // 指せない) なので、見つかった時点で終了してよい。
            break;
        }
    }

    // --- カーソル形状: リサイズ中ならその縁、そうでなければ手前のウィンドウの縁にホバーしているかを見る ---
    {
        uint32_t hoverEdges = 0;
        if (isResizingThisFrame) {
            if (UiWindow* window = GetComponent<UiWindow>(resizingEntity)) {
                hoverEdges = window->resizeEdges;
            }
        } else {
            for (int32_t surfaceId : activeSurfaces) {
                if (!isUnderCursor(surfaceId)) {
                    continue;
                }
                const Vec2f cursor   = surfaceCursor(surfaceId);
                EntityHandle hovered = FindFrontMostWindowAt(surfaceId, cursor);
                if (hovered.IsValid()) {
                    UiWindow* window       = GetComponent<UiWindow>(hovered);
                    UiTransform* transform = GetComponent<UiTransform>(hovered);
                    if (window && transform && window->resizable) {
                        hoverEdges = ComputeResizeEdges(*transform, cursor);
                    }
                }
                break;
            }
        }
        UpdateCursorShape(hoverEdges);
    }

    // --- 前面化: カーソルを含むウィンドウのうち一番手前のものを前に出す ---
    // 中身のどの要素がクリックされたかを辿る必要はなく、ウィンドウ矩形の点判定で足りる。
    if (triggered) {
        for (int32_t surfaceId : activeSurfaces) {
            if (!isUnderCursor(surfaceId)) {
                continue;
            }
            const Vec2f cursor       = surfaceCursor(surfaceId);
            EntityHandle frontTarget = FindFrontMostWindowAt(surfaceId, cursor);
            if (frontTarget.IsValid()) {
                BringToFront(frontTarget);
            }
            break;
        }
    }

    // --- 移動: タイトルバーが押されている間、掴んだ位置を保つように動かす ---
    // リサイズ中のウィンドウがあるフレームは移動を行わない。
    for (const auto& entity : entities_) {
        UiTransform* transform = GetComponent<UiTransform>(entity);
        UiWindow* window       = GetComponent<UiWindow>(entity);
        if (!transform || !window) {
            continue;
        }
        if (isResizingThisFrame || !window->movable || !window->titleBar.IsValid()) {
            window->isDragging = false;
            continue;
        }

        UiInteractable* titleInteractable = GetComponent<UiInteractable>(window->titleBar);
        if (!titleInteractable || !titleInteractable->isPressed) {
            window->isDragging = false;
            continue;
        }

        // このウィンドウが乗っているサーフェス基準のカーソル座標で動かす
        // (切り離し済みのウィンドウは movable = false になるので、実質メインウィンドウのみ通る)。
        const Vec2f cursor = surfaceCursor(transform->resolvedSurfaceId);

        if (!window->isDragging) {
            window->isDragging     = true;
            window->dragGrabOffset = {cursor[X] - transform->resolvedMin[X],
                                       cursor[Y] - transform->resolvedMin[Y]};
        }

        // v10: メインウィンドウ上のウィンドウをタイトルバーでドラッグし、クライアント矩形の外へ
        // 一定距離出たら切り離し要求を立てる (Blender/Visual Studio と同じ操作感)。
        // このフレームはこれ以上位置を更新せず、実際の OS ウィンドウ生成はアプリ側に任せる。
        if (transform->resolvedSurfaceId == 0 && surfaceProvider_ != nullptr) {
            POINT screenCursor{};
            GetCursorPos(&screenCursor);
            const Vec2f mainOrigin = surfaceProvider_->GetSurfaceScreenOrigin(0);
            const Vec2f mainSize   = surfaceProvider_->GetSurfaceSize(0);
            const bool outside =
                static_cast<float>(screenCursor.x) < mainOrigin[X] - kDetachMargin ||
                static_cast<float>(screenCursor.y) < mainOrigin[Y] - kDetachMargin ||
                static_cast<float>(screenCursor.x) > mainOrigin[X] + mainSize[X] + kDetachMargin ||
                static_cast<float>(screenCursor.y) > mainOrigin[Y] + mainSize[Y] + kDetachMargin;
            if (outside) {
                window->detachRequested = true;
                window->isDragging      = false;
                continue;
            }
        }

        // 点アンカー前提。offsetMin / offsetMax は画面左上からの絶対ピクセル。
        const Vec2f size    = {transform->offsetMax[X] - transform->offsetMin[X],
                                transform->offsetMax[Y] - transform->offsetMin[Y]};
        const Vec2f topLeft = {cursor[X] - window->dragGrabOffset[X],
                                cursor[Y] - window->dragGrabOffset[Y]};

        transform->offsetMin = topLeft;
        transform->offsetMax = {topLeft[X] + size[X], topLeft[Y] + size[Y]};
    }

    // --- 閉じる / 切り離し ---
    for (const auto& entity : entities_) {
        UiWindow* window = GetComponent<UiWindow>(entity);
        if (!window) {
            continue;
        }

        // クリックされたかどうかだけ見る。実際にエンティティを破棄する（あるいは v9/v10 で
        // 別ウィンドウへ移す）のはアプリの責務なので、ここでは要求フラグを立てるだけにする。
        if (UiInteractable* closeInteractable = GetComponent<UiInteractable>(window->closeButton)) {
            if (closeInteractable->wasClicked) {
                window->closeRequested = true;
            }
        }
        if (UiInteractable* detachInteractable = GetComponent<UiInteractable>(window->detachButton)) {
            if (detachInteractable->wasClicked) {
                window->detachRequested = true;
            }
        }

        // closable / resizable に応じてボタンの見た目と反応を毎フレーム設定する。
        // detachButton には対応する設定項目が無いため常に表示・有効のままにする。
        // v10: 切り離し済みのウィンドウはタイトルバー自体をアプリ側 (TerminalApp) が
        // visible = false にして隠している。closeButton はタイトルバーの子なので、
        // ここで無条件に closable の値へ戻すとタイトルバーの無い場所にボタンだけ浮いてしまう。
        // タイトルバーが見えているときだけ、closable に応じた表示に戻す。
        UiTransform* titleTransform = GetComponent<UiTransform>(window->titleBar);
        const bool titleBarVisible  = titleTransform == nullptr || titleTransform->visible;
        if (UiTransform* closeTransform = GetComponent<UiTransform>(window->closeButton)) {
            closeTransform->visible = window->closable && titleBarVisible;
        }
        if (UiInteractable* closeInteractable = GetComponent<UiInteractable>(window->closeButton)) {
            closeInteractable->enabled = window->closable;
        }
    }
}

void UiWindowSystem::BringToFront(const EntityHandle& _window) {
    UiWindow* target             = GetComponent<UiWindow>(_window);
    UiTransform* targetTransform = GetComponent<UiTransform>(_window);
    if (!target || !targetTransform) {
        return;
    }
    // v10: 前後関係はサーフェスごとに独立している。別の OS ウィンドウに出ているウィンドウの
    // 前後関係は OS が決めるので、自作の order / renderPriority は同じサーフェス内だけで振り直す。
    const int32_t surfaceId = targetTransform->resolvedSurfaceId;

    // 既に最前面なら何もしない（毎クリックで振り直すのを避ける）
    bool alreadyFront = true;
    for (const auto& entity : entities_) {
        UiWindow* window       = GetComponent<UiWindow>(entity);
        UiTransform* transform = GetComponent<UiTransform>(entity);
        if (window && window != target && transform && transform->resolvedSurfaceId == surfaceId &&
            window->order > target->order) {
            alreadyFront = false;
            break;
        }
    }
    if (alreadyFront) {
        return;
    }

    // 対象を一番大きい order にしてから、0 から詰め直す (同じサーフェスのウィンドウだけ対象)。
    int32_t maxOrder = target->order;
    for (const auto& entity : entities_) {
        UiWindow* window       = GetComponent<UiWindow>(entity);
        UiTransform* transform = GetComponent<UiTransform>(entity);
        if (window && transform && transform->resolvedSurfaceId == surfaceId) {
            maxOrder = std::max(maxOrder, window->order);
        }
    }
    target->order = maxOrder + 1;

    // order の昇順に並べ直して 0..n-1 を振り、優先度の帯を割り当てる。
    std::vector<std::pair<int32_t, EntityHandle>> sorted;
    sorted.reserve(entities_.size());
    for (const auto& entity : entities_) {
        UiWindow* window       = GetComponent<UiWindow>(entity);
        UiTransform* transform = GetComponent<UiTransform>(entity);
        if (window && transform && transform->resolvedSurfaceId == surfaceId) {
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

EntityHandle UiWindowSystem::FindFrontMostWindowAt(int32_t _surfaceId, const Vec2f& _cursor) {
    EntityHandle frontTarget{};
    int32_t bestOrder = 0;
    bool found        = false;

    for (const auto& entity : entities_) {
        UiTransform* transform = GetComponent<UiTransform>(entity);
        UiWindow* window       = GetComponent<UiWindow>(entity);
        if (!transform || !window || !transform->resolvedVisible) {
            continue;
        }
        if (transform->resolvedSurfaceId != _surfaceId) {
            continue;
        }
        // v14: ドックされているウィンドウは前面化 (BringToFront) にもリサイズ枠判定にも
        // 使わない (このメソッドは両方で共有されているため、ここで弾けば両方から自然に外れる)。
        // movable/resizable は false になるので移動/リサイズ自体は既に弾かれているが、
        // ここを弾かないとクリックのたびに「同じサーフェス上の他のフローティングウィンドウ」の
        // 前後関係まで振り直されてしまう。
        if (window->IsDocked()) {
            continue;
        }
        const bool inside =
            _cursor[X] >= transform->resolvedMin[X] && _cursor[X] < transform->resolvedMax[X] &&
            _cursor[Y] >= transform->resolvedMin[Y] && _cursor[Y] < transform->resolvedMax[Y];
        if (!inside) {
            continue;
        }
        if (!found || window->order > bestOrder) {
            frontTarget = entity;
            bestOrder   = window->order;
            found       = true;
        }
    }

    return frontTarget;
}

uint32_t UiWindowSystem::ComputeResizeEdges(const UiTransform& _transform, const Vec2f& _cursor) {
    uint32_t edges = 0;

    // _cursor は既にこの矩形の内側にある前提 (FindFrontMostWindowAt の点判定を通過済み) なので、
    // 各縁からの距離だけを見ればよい。角は 2 ビット立つ。
    if (_cursor[X] <= _transform.resolvedMin[X] + kResizeBorder) {
        edges |= UiWindow::kEdgeLeft;
    }
    if (_cursor[X] >= _transform.resolvedMax[X] - kResizeBorder) {
        edges |= UiWindow::kEdgeRight;
    }
    if (_cursor[Y] <= _transform.resolvedMin[Y] + kResizeBorder) {
        edges |= UiWindow::kEdgeTop;
    }
    if (_cursor[Y] >= _transform.resolvedMax[Y] - kResizeBorder) {
        edges |= UiWindow::kEdgeBottom;
    }

    return edges;
}

void UiWindowSystem::ApplyResizeDrag(const UiWindow& _window, UiTransform& _transform, const Vec2f& _cursor) {
    const Vec2f delta = {_cursor[X] - _window.resizeStartCursor[X],
                          _cursor[Y] - _window.resizeStartCursor[Y]};

    Vec2f newMin = _window.resizeStartMin;
    Vec2f newMax = _window.resizeStartMax;

    // 左/上のエッジは min を、右/下のエッジは max を動かす。minSize を下回らないようクランプする。
    if (_window.resizeEdges & UiWindow::kEdgeLeft) {
        newMin[X] = std::min(_window.resizeStartMin[X] + delta[X], newMax[X] - _window.minSize[X]);
    }
    if (_window.resizeEdges & UiWindow::kEdgeRight) {
        newMax[X] = std::max(_window.resizeStartMax[X] + delta[X], newMin[X] + _window.minSize[X]);
    }
    if (_window.resizeEdges & UiWindow::kEdgeTop) {
        newMin[Y] = std::min(_window.resizeStartMin[Y] + delta[Y], newMax[Y] - _window.minSize[Y]);
    }
    if (_window.resizeEdges & UiWindow::kEdgeBottom) {
        newMax[Y] = std::max(_window.resizeStartMax[Y] + delta[Y], newMin[Y] + _window.minSize[Y]);
    }

    _transform.offsetMin = newMin;
    _transform.offsetMax = newMax;
}

void UiWindowSystem::UpdateCursorShape(uint32_t _edges) {
    Engine* engine = Engine::GetInstance();
    WinApp* winApp = engine ? engine->GetWinApp() : nullptr;
    if (!winApp) {
        return;
    }

    const uint32_t horizontal = _edges & (UiWindow::kEdgeLeft | UiWindow::kEdgeRight);
    const uint32_t vertical   = _edges & (UiWindow::kEdgeTop | UiWindow::kEdgeBottom);

    HCURSOR cursor = nullptr;
    if (horizontal != 0 && vertical != 0) {
        // 左上/右下の角なら \、右上/左下の角なら / のカーソルにする。
        const bool isBackSlash =
            (_edges & UiWindow::kEdgeLeft && _edges & UiWindow::kEdgeTop) ||
            (_edges & UiWindow::kEdgeRight && _edges & UiWindow::kEdgeBottom);
        cursor = ::LoadCursorW(nullptr, isBackSlash ? IDC_SIZENWSE : IDC_SIZENESW);
    } else if (horizontal != 0) {
        cursor = ::LoadCursorW(nullptr, IDC_SIZEWE);
    } else if (vertical != 0) {
        cursor = ::LoadCursorW(nullptr, IDC_SIZENS);
    }

    // cursor が nullptr のとき (縁の上に何も無いとき) は override を解除する。
    winApp->SetCursorShapeOverride(cursor);
}

} // namespace LogGuide
