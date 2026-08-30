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

// v15: UpdateDropTarget() がドラッグ中のウィンドウを探すのに、UiWindow コンポーネント配列を
// 直接 (登録されているエンティティ集合とは関係なく) 総当たりする必要があるため、
// ComponentArray<T>::GetSlotsRef() を直接使う。ISystem.h 経由で間接的には入っているが、
// 直接使うヘッダなので明示的に include する。
#include "component/ComponentArray.h"

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
            UpdateLeafNode(entity, *node);
        }
    }

    // v15: ドラッグ中のウィンドウのドロップ先判定/オーバーレイ表示/ドロップ実行。
    UpdateDropTarget(released);
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

void UiDockSystem::UpdateLeafNode(const EntityHandle& _leaf, UiDockNode& _node) {
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

    // v15: タブを押したまま一定距離動かしたら引き剥がし要求を積む。
    // (wasClicked は release されたフレームにしか立たないため、ここより前で判定しても
    // クリックとしての activeTab 切り替えとは競合しない)。
    HandleTabTearOff(_leaf, _node);

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

void UiDockSystem::HandleTabTearOff(const EntityHandle& _leaf, UiDockNode& _node) {
    // 現在押されているタブを 1 つ探す (複数同時押しは無い想定)。
    int32_t pressedIndex = -1;
    for (size_t i = 0; i < _node.tabButtons.size(); ++i) {
        if (UiInteractable* interactable = GetComponent<UiInteractable>(_node.tabButtons[i])) {
            if (interactable->isPressed) {
                pressedIndex = static_cast<int32_t>(i);
                break;
            }
        }
    }

    if (pressedIndex < 0) {
        // 何も押されていない: このリーフに対する追跡があれば消す (次の押下でまた最初から測る)。
        if (tabTear_.leaf == _leaf) {
            tabTear_ = TabTearState{};
        }
        return;
    }

    // ドックスペースはサーフェス 0 (メインウィンドウ) にしか無い前提 (v15 の「やらないこと」参照)。
    const Vec2f cursor = SurfaceCursor(0);

    if (tabTear_.leaf != _leaf || tabTear_.tabIndex != pressedIndex) {
        // 新しく押され始めた (あるいは別のタブ/葉へ移った): 押下位置から測り直す。
        tabTear_.leaf        = _leaf;
        tabTear_.tabIndex    = pressedIndex;
        tabTear_.pressCursor = cursor;
        tabTear_.torn        = false;
        return;
    }

    if (tabTear_.torn) {
        return; // 既に引き剥がし要求を積んだ (アプリの処理待ち)。同じ押下中は積み直さない。
    }

    const float dx = cursor[X] - tabTear_.pressCursor[X];
    const float dy = cursor[Y] - tabTear_.pressCursor[Y];
    if (dx * dx + dy * dy < kTearOffDistance * kTearOffDistance) {
        return; // まだ閾値未満。
    }

    if (pressedIndex >= static_cast<int32_t>(_node.windows.size())) {
        return; // 想定外 (タブボタン数と windows 数がずれている)。
    }
    const EntityHandle windowHandle = _node.windows[pressedIndex];
    UiWindow* window                = GetComponent<UiWindow>(windowHandle);
    if (!window) {
        return;
    }

    // アンドック後の大きさは、ドックされる前のフローティング時の矩形 (floatingSize) を使う。
    // タイトルバーの中心あたりを掴んだことにして、そのままドラッグへ引き継ぐ。
    const Vec2f size       = window->floatingSize;
    const Vec2f grabOffset = {size[X] * 0.5f, kUiWindowTitleBarHeight * 0.5f};
    const Vec2f position   = {cursor[X] - grabOffset[X], cursor[Y] - grabOffset[Y]};

    tearOffRequests_.push_back(UiDockTearOffRequest{windowHandle, position, grabOffset});
    tabTear_.torn = true;
}

void UiDockSystem::UpdateDropTarget(bool _released) {
    UiTransform* overlayTransform = dropOverlay_.IsValid() ? GetComponent<UiTransform>(dropOverlay_) : nullptr;

    // 1. 現在ドラッグ中のフローティングウィンドウを探す。
    // release されたフレームは UiWindowSystem (StateTransition 内でこちらより先に実行される)
    // が titleBar の isPressed 解除を見て先に isDragging を false に戻してしまっているため、
    // 直前フレームまで追跡していた activeDragWindow_ を release 判定にも使う。
    EntityHandle draggingWindow{};
    if (ComponentArray<UiWindow>* windowArray = GetComponentArray<UiWindow>()) {
        for (auto& slot : windowArray->GetSlotsRef()) {
            for (UiWindow& window : slot.components) {
                if (window.isDragging && !window.IsDocked()) {
                    draggingWindow = slot.owner;
                    break;
                }
            }
            if (draggingWindow.IsValid()) {
                break;
            }
        }
    }

    if (draggingWindow.IsValid()) {
        activeDragWindow_ = draggingWindow;
    } else if (_released && activeDragWindow_.IsValid()) {
        draggingWindow = activeDragWindow_; // release フレーム分の判定に使う。
    } else {
        activeDragWindow_ = {};
    }

    UiTransform* windowTransform = draggingWindow.IsValid() ? GetComponent<UiTransform>(draggingWindow) : nullptr;
    if (!windowTransform) {
        dropTarget_ = UiDockDropTarget{};
        activeDragWindow_ = {};
        if (overlayTransform) {
            overlayTransform->visible = false;
        }
        return;
    }

    // v10 の「メインウィンドウの外へ 24px 出したら OS ウィンドウへ切り離す」判定は
    // UiWindowSystem (こちらより先に実行される) が既に見ている。切り離しが起きるフレームは
    // そちらが isDragging を false に戻して detachRequested を立てるため、上の探索で
    // draggingWindow が見つからなくなり、この関数はここまでで抜ける
    // (= ドロップ先は出さない。優先順位 1. の実現)。

    const int32_t surfaceId = windowTransform->resolvedSurfaceId;

    // v16 改: ドロップ先の判定には「カーソル 1 点」を使う。
    // 最初はドラッグ中のウィンドウのタイトルバー矩形と相手の帯との重なり面積で判定していたが、
    // タイトルバーは幅が数百 px あるため、カーソルがまるで別の場所にあっても端がかすっただけで
    // 判定が出てしまい、「縁の判定がやたら大きい」という操作感になった。
    // カーソル 1 点なら、狙った場所だけが素直に反応する。
    Scene* scene       = GetScene();
    MouseInput* mouse  = scene ? scene->GetMouseInput() : nullptr;
    const Vec2f cursor = surfaceProvider_
                             ? surfaceProvider_->GetSurfaceCursorPos(surfaceId)
                             : (mouse ? mouse->GetPosition() : Vec2f{-1.0f, -1.0f});

    auto contains = [](const Vec2f& _min, const Vec2f& _max, const Vec2f& _p) {
        return _p[X] >= _min[X] && _p[X] < _max[X] && _p[Y] >= _min[Y] && _p[Y] < _max[Y];
    };

    // 2. カーソルを含む葉ノードを 1 つ見つけ、その中のどこを指しているかで区画を決める。
    // 葉ノード同士は重ならないので、最初に見つかったものがそのまま答えになる。
    //   タブバーの上 … タブとして結合 (Center)
    //   内容領域の縁 … その辺へ分割 (一番近い辺を採る)
    //   それ以外     … ドロップ先なし (中央を素通りしても何も出ない)
    EntityHandle hitLeaf{};
    UiDockNode* hitNode = nullptr;
    UiDockDropZone zone = UiDockDropZone::None;

    for (const EntityHandle& entity : entities_) {
        UiDockNode* node       = GetComponent<UiDockNode>(entity);
        UiTransform* transform = GetComponent<UiTransform>(entity);
        if (!node || !transform || node->split != UiDockSplit::None) {
            continue;
        }
        if (transform->resolvedSurfaceId != surfaceId || !transform->resolvedVisible) {
            continue;
        }

        UiTransform* tabBarTransform  = GetComponent<UiTransform>(node->tabBar);
        UiTransform* contentTransform = GetComponent<UiTransform>(node->contentArea);

        // タブ結合を先に見る (タブバーと内容領域は重ならないので、実際には排他)。
        if (tabBarTransform && contains(tabBarTransform->resolvedMin, tabBarTransform->resolvedMax, cursor)) {
            hitLeaf = entity;
            hitNode = node;
            zone    = UiDockDropZone::Center;
            break;
        }

        if (contentTransform && contains(contentTransform->resolvedMin, contentTransform->resolvedMax, cursor)) {
            const Vec2f contentMin = contentTransform->resolvedMin;
            const Vec2f contentMax = contentTransform->resolvedMax;

            const float toLeft   = cursor[X] - contentMin[X];
            const float toRight  = contentMax[X] - cursor[X];
            const float toTop    = cursor[Y] - contentMin[Y];
            const float toBottom = contentMax[Y] - cursor[Y];

            float nearest              = toLeft;
            UiDockDropZone nearestZone = UiDockDropZone::Left;
            if (toRight < nearest) {
                nearest     = toRight;
                nearestZone = UiDockDropZone::Right;
            }
            if (toTop < nearest) {
                nearest     = toTop;
                nearestZone = UiDockDropZone::Top;
            }
            if (toBottom < nearest) {
                nearest     = toBottom;
                nearestZone = UiDockDropZone::Bottom;
            }

            // どの辺からも kUiDockSplitBand より離れていれば中央 = ドロップ先なし。
            if (nearest <= kUiDockSplitBand) {
                hitLeaf = entity;
                hitNode = node;
                zone    = nearestZone;
            }
            break; // カーソルを含む葉ノードは 1 つだけ。中央だった場合もここで確定。
        }
    }

    if (!hitLeaf.IsValid() || !hitNode) {
        dropTarget_ = UiDockDropTarget{};
        if (overlayTransform) {
            overlayTransform->visible = false;
        }
        if (_released) {
            activeDragWindow_ = {}; // このフレームで確定 (ドロップ無し。その場に浮いたまま)。
        }
        return;
    }

    dropTarget_.leaf = hitLeaf;
    dropTarget_.zone = zone;

    // 3. オーバーレイの矩形を「実際にそこへ落とした結果占める領域」に合わせる。
    if (overlayTransform) {
        Vec2f overlayMin{};
        Vec2f overlayMax{};

        if (zone == UiDockDropZone::Center) {
            // v16: 「ここに並ぶ」ことが分かるよう、ペイン全体ではなく相手のタブバーを示す。
            if (UiTransform* tabBarTransform = GetComponent<UiTransform>(hitNode->tabBar)) {
                overlayMin = tabBarTransform->resolvedMin;
                overlayMax = tabBarTransform->resolvedMax;
            }
        } else if (UiTransform* hitTransform = GetComponent<UiTransform>(hitLeaf)) {
            // 葉ノード (タブバーを含む矩形全体) を既定の比率 0.5 で割った側の領域。
            overlayMin             = hitTransform->resolvedMin;
            overlayMax             = hitTransform->resolvedMax;
            const float halfW = (hitTransform->resolvedMax[X] - hitTransform->resolvedMin[X]) * 0.5f;
            const float halfH = (hitTransform->resolvedMax[Y] - hitTransform->resolvedMin[Y]) * 0.5f;
            switch (zone) {
            case UiDockDropZone::Left:
                overlayMax[X] = hitTransform->resolvedMin[X] + halfW;
                break;
            case UiDockDropZone::Right:
                overlayMin[X] = hitTransform->resolvedMax[X] - halfW;
                break;
            case UiDockDropZone::Top:
                overlayMax[Y] = hitTransform->resolvedMin[Y] + halfH;
                break;
            case UiDockDropZone::Bottom:
                overlayMin[Y] = hitTransform->resolvedMax[Y] - halfH;
                break;
            default:
                break;
            }
        }

        overlayTransform->surfaceId = surfaceId;
        overlayTransform->offsetMin = overlayMin;
        overlayTransform->offsetMax = overlayMax;
        overlayTransform->visible   = true; // ここに来た時点で zone は None ではない。
    }

    // 4. 離されたらドック要求を積む。
    if (_released) {
        dockRequests_.push_back(UiDockRequest{draggingWindow, hitLeaf, zone});
        activeDragWindow_ = {}; // 消費したので追跡終了。
        if (overlayTransform) {
            overlayTransform->visible = false; // ドロップ後はオーバーレイを消す。
        }
    }
}

std::vector<UiDockRequest> UiDockSystem::TakeDockRequests() {
    std::vector<UiDockRequest> taken;
    taken.swap(dockRequests_);
    return taken;
}

std::vector<UiDockTearOffRequest> UiDockSystem::TakeTearOffRequests() {
    std::vector<UiDockTearOffRequest> taken;
    taken.swap(tearOffRequests_);
    return taken;
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
