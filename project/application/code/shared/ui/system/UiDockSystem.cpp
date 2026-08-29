#include "ui/system/UiDockSystem.h"

/// engine
#include "Engine.h"
#include "component/text/TextComponent.h"
// Scene.h は MouseInput を前方宣言しているだけなので、GetPosition() 等を呼ぶには定義が要る。
#include "input/MouseInput.h"
#include "scene/Scene.h"
#include "system/SystemRunner.h"
#include "winApp/WinApp.h"

/// application
#include "ui/UiDockBuilder.h" // kUiDockTabWidth 等の定数
#include "ui/UiWidgetBuilder.h" // CreateUiButton (タブボタン)
#include "ui/UiWindowBuilder.h" // HideUiWindowChrome (v10 と共通のタイトルバー非表示処理)
#include "ui/component/UiInteractable.h"
#include "ui/component/UiWindow.h"
#include "ui/native/NativeWindowManager.h"

/// stl
#include <algorithm>

using namespace OriGine;

namespace LogGuide {

void UiDockSystem::Initialize() {}

void UiDockSystem::Finalize() {}

void UiDockSystem::Update() {
    if (entities_.empty()) {
        return;
    }
    EraseDeadEntity();

    Scene* scene = GetScene();
    if (!scene) {
        return;
    }

    // v10 と同じく、リサイズ継続中と同様に released は毎フレーム 1 回だけ判定すればよい。
    MouseInput* mouse    = scene->GetMouseInput();
    const bool released  = surfaceProvider_ ? surfaceProvider_->IsMouseRelease()
                                             : (mouse ? mouse->IsRelease(MouseButton::LEFT) : false);

    for (const auto& entity : entities_) {
        UiDockNode* node       = GetComponent<UiDockNode>(entity);
        UiTransform* transform = GetComponent<UiTransform>(entity);
        if (!node || !transform) {
            continue;
        }

        if (node->split != UiDockSplit::None) {
            UpdateSplitNode(*node, *transform, released);
        } else {
            UpdateLeafNode(*node);
        }
    }
}

void UiDockSystem::UpdateSplitNode(UiDockNode& _node, UiTransform& _transform, bool _released) {
    const bool isHorizontal = (_node.split == UiDockSplit::Horizontal);
    const float half        = kUiDockSplitterWidth * 0.5f;

    // 1. childA / childB / スプリッターのアンカーを splitRatio から決める。
    // ドック専用のレイアウト計算はしない方針のため、ここで決めるのは「アンカーの割合」だけで、
    // 実際の矩形計算 (resolvedMin/Max) は UiLayoutSystem に任せる。
    if (UiTransform* childATransform = GetComponent<UiTransform>(_node.childA)) {
        childATransform->anchorMin = {0.0f, 0.0f};
        childATransform->anchorMax = isHorizontal ? Vec2f{_node.splitRatio, 1.0f} : Vec2f{1.0f, _node.splitRatio};
        childATransform->offsetMin = {0.0f, 0.0f};
        childATransform->offsetMax = {0.0f, 0.0f};
    }
    if (UiTransform* childBTransform = GetComponent<UiTransform>(_node.childB)) {
        childBTransform->anchorMin = isHorizontal ? Vec2f{_node.splitRatio, 0.0f} : Vec2f{0.0f, _node.splitRatio};
        childBTransform->anchorMax = {1.0f, 1.0f};
        childBTransform->offsetMin = {0.0f, 0.0f};
        childBTransform->offsetMax = {0.0f, 0.0f};
    }
    UiTransform* splitterTransform = GetComponent<UiTransform>(_node.splitter);
    if (splitterTransform) {
        if (isHorizontal) {
            splitterTransform->anchorMin = {_node.splitRatio, 0.0f};
            splitterTransform->anchorMax = {_node.splitRatio, 1.0f};
            splitterTransform->offsetMin = {-half, 0.0f};
            splitterTransform->offsetMax = {half, 0.0f};
        } else {
            splitterTransform->anchorMin = {0.0f, _node.splitRatio};
            splitterTransform->anchorMax = {1.0f, _node.splitRatio};
            splitterTransform->offsetMin = {0.0f, -half};
            splitterTransform->offsetMax = {0.0f, half};
        }
    }

    // 2/3. スプリッターのドラッグと、ホバー中のカーソル形状。
    UiInteractable* splitterInteractable = GetComponent<UiInteractable>(_node.splitter);
    if (!splitterInteractable || !splitterTransform) {
        return;
    }

    const int32_t surfaceId = _transform.resolvedSurfaceId;
    const Vec2f cursor      = SurfaceCursor(surfaceId);

    if (splitterInteractable->isPressed) {
        if (!_node.isDraggingSplitter) {
            // ドラッグ開始: 掴んだ位置 (スプリッター中心からカーソルまでの距離) を覚えておく
            // (UiWindowSystem::dragGrabOffset / UiScrollSystem::thumbGrabOffset と同じ考え方。
            // 掴んだ場所がスプリッターの端でも、ドラッグ開始時にガクッと動かないようにする)。
            _node.isDraggingSplitter = true;
            const float splitterCenter = isHorizontal
                ? (splitterTransform->resolvedMin[X] + splitterTransform->resolvedMax[X]) * 0.5f
                : (splitterTransform->resolvedMin[Y] + splitterTransform->resolvedMax[Y]) * 0.5f;
            _node.splitterGrabOffset = (isHorizontal ? cursor[X] : cursor[Y]) - splitterCenter;
        }

        // このノード自身の矩形 (前フレームの UiLayoutSystem の結果) に対する比率として求め直す。
        const float nodeMin  = isHorizontal ? _transform.resolvedMin[X] : _transform.resolvedMin[Y];
        const float nodeSize = isHorizontal
            ? (_transform.resolvedMax[X] - _transform.resolvedMin[X])
            : (_transform.resolvedMax[Y] - _transform.resolvedMin[Y]);
        const float desiredCenter = (isHorizontal ? cursor[X] : cursor[Y]) - _node.splitterGrabOffset;

        if (nodeSize > 0.0f) {
            _node.splitRatio = std::clamp((desiredCenter - nodeMin) / nodeSize, 0.05f, 0.95f);
        }

        ClaimSplitterCursor(isHorizontal);

        if (_released) {
            _node.isDraggingSplitter = false;
        }
    } else {
        _node.isDraggingSplitter = false;

        // ドラッグ中でなくても、ホバーしているだけならカーソル形状は変える
        // (UiWindowSystem のリサイズ縁ホバーと同じ挙動)。
        if (splitterInteractable->isHovered && IsSurfaceUnderCursor(surfaceId)) {
            ClaimSplitterCursor(isHorizontal);
        }
    }
}

void UiDockSystem::UpdateLeafNode(UiDockNode& _node) {
    // 1. タブの作り直し。
    if (_node.tabsDirty) {
        RebuildTabButtons(_node);
        _node.tabsDirty = false;
    }

    // タブが減った等で範囲外になっていないようクランプしておく (保険)。
    _node.activeTab = _node.windows.empty()
        ? 0
        : std::clamp(_node.activeTab, 0, static_cast<int32_t>(_node.windows.size()) - 1);

    // 2. タブのクリックで activeTab を切り替える。
    for (size_t i = 0; i < _node.tabButtons.size(); ++i) {
        if (UiInteractable* interactable = GetComponent<UiInteractable>(_node.tabButtons[i])) {
            if (interactable->wasClicked) {
                _node.activeTab = static_cast<int32_t>(i);
            }
        }
    }
    // activeTab のタブだけ選択色にする (v12 の isSelected)。
    for (size_t i = 0; i < _node.tabButtons.size(); ++i) {
        if (UiInteractable* interactable = GetComponent<UiInteractable>(_node.tabButtons[i])) {
            interactable->isSelected = (static_cast<int32_t>(i) == _node.activeTab);
        }
    }

    // 3. windows の各ウィンドウのルートを内容領域に合わせ、activeTab のものだけ表示する。
    Scene* scene = GetScene();
    for (size_t i = 0; i < _node.windows.size(); ++i) {
        const EntityHandle& windowRoot = _node.windows[i];
        UiWindow* window              = GetComponent<UiWindow>(windowRoot);
        UiTransform* windowTransform  = GetComponent<UiTransform>(windowRoot);
        if (!window || !windowTransform) {
            continue;
        }

        windowTransform->parent    = _node.contentArea;
        windowTransform->anchorMin = {0.0f, 0.0f};
        windowTransform->anchorMax = {1.0f, 1.0f};
        windowTransform->offsetMin = {0.0f, 0.0f};
        windowTransform->offsetMax = {0.0f, 0.0f};
        windowTransform->visible   = (static_cast<int32_t>(i) == _node.activeTab);
        // フローティング時の重なり順 (_order * kWindowPriorityBand) をここでは 0 にリセットする。
        // スプリッターは境界の左右/上下 3px ずつ、隣の葉ノードの内容領域と意図的に重ねて
        // 置いている (UiDockNode.h 参照) ため、ドックされたウィンドウの子孫が
        // resolvedPriority で上回るとスプリッターがクリックできなくなってしまう。
        // ドック中は複数ウィンドウの重なり順そのものが意味を持たない (非アクティブなタブは
        // 不可視になるだけ) ので、0 にリセットしてスプリッター (renderPriority=4) に譲る。
        windowTransform->renderPriority = 0;

        // 位置と大きさはドックが決めるので、ウィンドウ自身の移動/リサイズは殺す。
        window->movable   = false;
        window->resizable = false;

        // タブがタイトルの役割を兼ねるため、自作タイトルバーは隠したままにする
        // (v10 の切り離しと共通の処理。毎フレーム呼んでも冪等なので安全)。
        if (scene) {
            HideUiWindowChrome(scene, *window);
        }
    }
}

void UiDockSystem::RebuildTabButtons(UiDockNode& _node) {
    Scene* scene          = GetScene();
    SystemRunner* runner  = scene ? scene->GetSystemRunnerRef() : nullptr;
    if (!scene) {
        return;
    }

    // 前回作ったタブボタンは必ず破棄してから作り直す (v13 の一覧の行と同じ作法。リークさせない)。
    for (const EntityHandle& button : _node.tabButtons) {
        scene->AddDeleteEntity(button);
    }
    _node.tabButtons.clear();

    if (!runner) {
        return; // SystemRunner が取れない (通常は無い経路)。次のフレームで再試行される。
    }

    for (size_t i = 0; i < _node.windows.size(); ++i) {
        UiWindow* window = GetComponent<UiWindow>(_node.windows[i]);
        std::string label = "Window";
        if (window) {
            if (TextComponent* titleLabel = GetComponent<TextComponent>(window->titleBar)) {
                label = titleLabel->text;
            }
        }

        UiWidgetDesc desc{};
        desc.parent         = _node.tabBar;
        desc.anchorMin      = {0.0f, 0.0f};
        desc.anchorMax      = {0.0f, 1.0f};
        desc.offsetMin      = {static_cast<float>(i) * kUiDockTabWidth, 0.0f};
        desc.offsetMax      = {static_cast<float>(i) * kUiDockTabWidth + kUiDockTabWidth, 0.0f};
        desc.renderPriority = 1; // タブバーの背景より手前に描く。
        desc.name           = "UiDockTabButton";

        _node.tabButtons.push_back(CreateUiButton(scene, runner, desc, label, 14.0f));
    }
}

Vec2f UiDockSystem::SurfaceCursor(int32_t _surfaceId) {
    if (surfaceProvider_) {
        return surfaceProvider_->GetSurfaceCursorPos(_surfaceId);
    }
    Scene* scene      = GetScene();
    MouseInput* mouse = scene ? scene->GetMouseInput() : nullptr;
    return mouse ? mouse->GetPosition() : Vec2f{0.0f, 0.0f};
}

bool UiDockSystem::IsSurfaceUnderCursor(int32_t _surfaceId) {
    return surfaceProvider_ ? surfaceProvider_->IsSurfaceUnderCursor(_surfaceId) : true;
}

void UiDockSystem::ClaimSplitterCursor(bool _isHorizontal) {
    Engine* engine = Engine::GetInstance();
    WinApp* winApp = engine ? engine->GetWinApp() : nullptr;
    if (!winApp) {
        return;
    }
    // UiWindowSystem::UpdateCursorShape() も同じ関数を毎フレーム呼んでいる。
    // ここは「実際にスプリッターがホバー/ドラッグされているときだけ」呼ばれる (Update() 参照) ので、
    // 何も主張しないフレームは UiWindowSystem 側の結果 (nullptr での解除を含む) をそのまま残せる。
    winApp->SetCursorShapeOverride(::LoadCursorW(nullptr, _isHorizontal ? IDC_SIZEWE : IDC_SIZENS));
}

} // namespace LogGuide
