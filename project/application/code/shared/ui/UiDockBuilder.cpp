#include "ui/UiDockBuilder.h"

/// engine
#include "scene/Scene.h"
#include "system/SystemRunner.h"

/// application
#include "ui/component/UiHighlight.h"
#include "ui/component/UiInteractable.h"
#include "ui/component/UiRect.h"
#include "ui/component/UiTransform.h"
#include "ui/component/UiWindow.h"
#include "ui/system/UiDockSystem.h"
#include "ui/system/UiHighlightSystem.h"
#include "ui/system/UiInteractionSystem.h"
#include "ui/system/UiLayoutSystem.h"
#include "ui/system/UiRenderSystem.h"
// ShowUiWindowChrome (v10 の再結合と共通の後始末) を使う。
#include "ui/UiWindowBuilder.h"

/// stl
#include <algorithm>
#include <utility>

using namespace OriGine;

namespace LogGuide {

namespace {

/// 葉ノードを 1 つ作る (UiDockNode::split は None のまま)。タブバー/内容領域も組み立てる。
/// _parent が無効なハンドルならサーフェス全体を親矩形とする (CreateUiDockSpace のルート用)。
EntityHandle CreateLeafNodeEntity(Scene* _scene, SystemRunner* _runner, const EntityHandle& _parent) {
    EntityHandle node = _scene->CreateEntity("UiDockNode");
    _scene->AddComponent<UiTransform>(node);
    _scene->AddComponent<UiDockNode>(node);

    if (UiTransform* transform = _scene->GetComponent<UiTransform>(node)) {
        transform->parent       = _parent;
        transform->anchorMin    = {0.0f, 0.0f};
        transform->anchorMax    = {1.0f, 1.0f};
        transform->offsetMin    = {0.0f, 0.0f};
        transform->offsetMax    = {0.0f, 0.0f};
        transform->clipChildren = true;
        // アンカーは分割ノードになったとき UiDockSystem が splitRatio から毎フレーム決め直すので、
        // ここでの値は「葉ノードのまま (親を全ストレッチで覆う)」だけを想定した初期値でよい。
    }

    EntityHandle tabBar = _scene->CreateEntity("UiDockTabBar");
    _scene->AddComponent<UiTransform>(tabBar);
    _scene->AddComponent<UiRect>(tabBar);
    if (UiTransform* transform = _scene->GetComponent<UiTransform>(tabBar)) {
        transform->parent    = node;
        transform->anchorMin = {0.0f, 0.0f};
        transform->anchorMax = {1.0f, 0.0f};
        transform->offsetMin = {0.0f, 0.0f};
        transform->offsetMax = {0.0f, kUiDockTabBarHeight};
    }
    if (UiRect* rect = _scene->GetComponent<UiRect>(tabBar)) {
        rect->fillColor    = {0.13f, 0.14f, 0.17f, 1.0f};
        rect->borderWidth  = 0.0f;
        rect->cornerRadius = {0.0f, 0.0f, 0.0f, 0.0f};
    }

    EntityHandle contentArea = _scene->CreateEntity("UiDockContentArea");
    _scene->AddComponent<UiTransform>(contentArea);
    if (UiTransform* transform = _scene->GetComponent<UiTransform>(contentArea)) {
        transform->parent       = node;
        transform->anchorMin    = {0.0f, 0.0f};
        transform->anchorMax    = {1.0f, 1.0f};
        transform->offsetMin    = {0.0f, kUiDockTabBarHeight};
        transform->offsetMax    = {0.0f, 0.0f};
        transform->clipChildren = true;
    }

    if (UiDockNode* dockNode = _scene->GetComponent<UiDockNode>(node)) {
        dockNode->tabBar      = tabBar;
        dockNode->contentArea = contentArea;
    }

    _runner->RegisterEntity<UiLayoutSystem, UiDockSystem>(node);
    _runner->RegisterEntity<UiLayoutSystem, UiRenderSystem>(tabBar);
    _runner->RegisterEntity<UiLayoutSystem>(contentArea);

    return node;
}

/// 分割ノードの境界に置くスプリッターを 1 つ作る. 実際のアンカー (境界の位置) は
/// UiDockSystem が毎フレーム親ノードの split/splitRatio から決める (ここではまだ決まらない)。
EntityHandle CreateSplitterEntity(Scene* _scene, SystemRunner* _runner, const EntityHandle& _parent) {
    EntityHandle splitter = _scene->CreateEntity("UiDockSplitter");
    _scene->AddComponent<UiTransform>(splitter);
    _scene->AddComponent<UiRect>(splitter);
    _scene->AddComponent<UiInteractable>(splitter);
    _scene->AddComponent<UiHighlight>(splitter);

    if (UiTransform* transform = _scene->GetComponent<UiTransform>(splitter)) {
        transform->parent = _parent;
        // 子ノード (renderPriority 0 のまま) より必ず手前に描いて掴めるようにする。
        transform->renderPriority = 4;
    }
    if (UiRect* rect = _scene->GetComponent<UiRect>(splitter)) {
        rect->borderWidth  = 0.0f;
        rect->cornerRadius = {0.0f, 0.0f, 0.0f, 0.0f};
    }
    if (UiHighlight* highlight = _scene->GetComponent<UiHighlight>(splitter)) {
        highlight->normalColor  = {0.30f, 0.32f, 0.38f, 1.0f};
        highlight->hoverColor   = {0.40f, 0.52f, 0.75f, 1.0f};
        highlight->pressedColor = {0.50f, 0.62f, 0.85f, 1.0f};
    }

    _runner->RegisterEntity<UiLayoutSystem, UiRenderSystem, UiInteractionSystem, UiHighlightSystem>(splitter);

    return splitter;
}

/// 葉ノードが空になったら、親の分割ノードを畳む (もう一方の子を親の位置へ繰り上げる)。
/// 空でなければ、あるいは畳む相手 (親) がいなければ何もしない。再帰的に上まで畳む。
void CollapseIfEmpty(Scene* _scene, const EntityHandle& _leaf) {
    UiDockNode* leafNode         = _scene->GetComponent<UiDockNode>(_leaf);
    UiTransform* leafTransform   = _scene->GetComponent<UiTransform>(_leaf);
    if (!leafNode || !leafTransform || leafNode->split != UiDockSplit::None || !leafNode->windows.empty()) {
        return;
    }

    const EntityHandle parentSplit = leafTransform->parent;
    if (!parentSplit.IsValid()) {
        return; // ルート (ドックスペース) なら畳む相手がいない。空のドックスペースのまま残す。
    }

    UiDockNode* parentNode = _scene->GetComponent<UiDockNode>(parentSplit);
    if (!parentNode || parentNode->split == UiDockSplit::None) {
        return; // 想定外の構造 (親が分割ノードでない)。壊さないよう何もしない。
    }

    const EntityHandle sibling = (parentNode->childA == _leaf) ? parentNode->childB : parentNode->childA;
    UiDockNode* siblingNode       = _scene->GetComponent<UiDockNode>(sibling);
    UiTransform* siblingTransform = _scene->GetComponent<UiTransform>(sibling);
    if (!siblingNode || !siblingTransform) {
        return;
    }

    // 空になった葉ノードと、この分割の境界にあったスプリッターは要らなくなるので破棄する。
    if (leafNode->tabBar.IsValid()) {
        _scene->AddDeleteEntity(leafNode->tabBar);
    }
    if (leafNode->contentArea.IsValid()) {
        _scene->AddDeleteEntity(leafNode->contentArea);
    }
    // タブバーの子として作った個々のタブボタンは、タブバー自身を消しても別エンティティなので
    // 生き残ってしまう。ここで確実に破棄する (原因A: 消し忘れると孤児のまま残り続ける)。
    for (const EntityHandle& tabButton : leafNode->tabButtons) {
        if (tabButton.IsValid()) {
            _scene->AddDeleteEntity(tabButton);
        }
    }
    _scene->AddDeleteEntity(_leaf);
    if (parentNode->splitter.IsValid()) {
        _scene->AddDeleteEntity(parentNode->splitter);
    }

    if (siblingNode->split == UiDockSplit::None) {
        // 兄弟が葉ノード: 親のエンティティを「兄弟の中身そのもの」に作り替える
        // (親は UiTransform::parent で他所から参照されている可能性があるため、親のエンティティ自体を
        // 生かしたまま中身だけ引き継ぐ。兄弟の器は空にして破棄する)。
        for (const EntityHandle& windowRoot : siblingNode->windows) {
            if (UiWindow* window = _scene->GetComponent<UiWindow>(windowRoot)) {
                window->dockNode = parentSplit;
            }
        }

        parentNode->split       = UiDockSplit::None;
        parentNode->splitRatio  = 0.5f;
        parentNode->childA      = {};
        parentNode->childB      = {};
        parentNode->splitter    = {};
        parentNode->tabBar      = siblingNode->tabBar;
        parentNode->contentArea = siblingNode->contentArea;
        parentNode->windows     = siblingNode->windows;
        parentNode->activeTab   = siblingNode->activeTab;
        parentNode->tabButtons  = std::move(siblingNode->tabButtons);
        parentNode->tabsDirty   = true;

        if (UiTransform* transform = _scene->GetComponent<UiTransform>(parentNode->tabBar)) {
            transform->parent = parentSplit;
        }
        if (UiTransform* transform = _scene->GetComponent<UiTransform>(parentNode->contentArea)) {
            transform->parent = parentSplit;
        }

        _scene->AddDeleteEntity(sibling); // 中身は親へ移したので、空になった器だけ破棄する。

        // 親が葉ノードに戻った結果さらに空 (中身が無かった) なら、再帰的に上まで畳む。
        CollapseIfEmpty(_scene, parentSplit);
    } else {
        // 兄弟が分割ノード: 親のエンティティを「兄弟の分割設定そのもの」に作り替える
        // (2 段以上ネストしたツリーの途中を畳む場合。孫の親付けだけ繰り上げ先へ張り替える)。
        parentNode->split       = siblingNode->split;
        parentNode->splitRatio  = siblingNode->splitRatio;
        parentNode->childA      = siblingNode->childA;
        parentNode->childB      = siblingNode->childB;
        parentNode->splitter    = siblingNode->splitter;
        parentNode->tabBar      = {};
        parentNode->contentArea = {};
        // 親は葉ノードから分割ノードに変わるので、葉ノード時代のフィールドも後始末する
        // (原因C: ここを空にしないと windows/tabButtons のハンドルが誰からも参照されない
        // 孤児として残り、tabButtons は二度と破棄されない)。
        parentNode->windows.clear();
        for (const EntityHandle& tabButton : parentNode->tabButtons) {
            if (tabButton.IsValid()) {
                _scene->AddDeleteEntity(tabButton);
            }
        }
        parentNode->tabButtons.clear();
        parentNode->activeTab = 0;
        parentNode->tabsDirty = false;

        if (UiTransform* transform = _scene->GetComponent<UiTransform>(parentNode->childA)) {
            transform->parent = parentSplit;
        }
        if (UiTransform* transform = _scene->GetComponent<UiTransform>(parentNode->childB)) {
            transform->parent = parentSplit;
        }
        if (UiTransform* transform = _scene->GetComponent<UiTransform>(parentNode->splitter)) {
            transform->parent = parentSplit;
        }

        _scene->AddDeleteEntity(sibling); // 中身 (孫 2 つ + スプリッター) は親へ移したので、器だけ破棄する。
        // 親は引き続き分割ノードなので、これ以上畳む必要はない。
    }
}

/// 葉ノードの windows からウィンドウを 1 つ取り除く。空になったら CollapseIfEmpty() を呼ぶ.
void RemoveWindowFromLeaf(Scene* _scene, const EntityHandle& _leaf, const EntityHandle& _window) {
    UiDockNode* node = _scene->GetComponent<UiDockNode>(_leaf);
    if (!node) {
        return;
    }
    auto it = std::find(node->windows.begin(), node->windows.end(), _window);
    if (it == node->windows.end()) {
        return;
    }
    node->windows.erase(it);
    node->tabsDirty = true;
    node->activeTab = node->windows.empty()
        ? 0
        : std::clamp(node->activeTab, 0, static_cast<int32_t>(node->windows.size()) - 1);

    CollapseIfEmpty(_scene, _leaf);
}

} // namespace

EntityHandle CreateUiDockSpace(Scene* _scene, SystemRunner* _runner, int32_t _surfaceId) {
    EntityHandle root = CreateLeafNodeEntity(_scene, _runner, EntityHandle{});
    if (UiTransform* transform = _scene->GetComponent<UiTransform>(root)) {
        transform->surfaceId = _surfaceId;
    }
    return root;
}

EntityHandle SplitUiDockNode(Scene* _scene, SystemRunner* _runner, const EntityHandle& _target,
                              UiDockSplit _split, float _ratio) {
    UiDockNode* targetNode = _scene->GetComponent<UiDockNode>(_target);
    if (!targetNode || targetNode->split != UiDockSplit::None) {
        return {}; // 葉ノードでなければ誤用 (無効なハンドルを返す)。
    }

    // 既存の中身 (タブバー/内容領域/ウィンドウ一覧) を退避する。
    const EntityHandle oldTabBar          = targetNode->tabBar;
    const EntityHandle oldContentArea     = targetNode->contentArea;
    std::vector<EntityHandle> oldWindows  = std::move(targetNode->windows);
    std::vector<EntityHandle> oldTabButtons = std::move(targetNode->tabButtons);
    const int32_t oldActiveTab            = targetNode->activeTab;

    // --- childA: 新しくできる、中身の無い空の葉ノード (_ratio 側) ---
    const EntityHandle childA = CreateLeafNodeEntity(_scene, _runner, _target);

    // --- childB: 既存の中身を引き継ぐ葉ノード。タブバー/内容領域は使い回す (作り直さない) ---
    EntityHandle childB = _scene->CreateEntity("UiDockNode");
    _scene->AddComponent<UiTransform>(childB);
    _scene->AddComponent<UiDockNode>(childB);
    if (UiTransform* transform = _scene->GetComponent<UiTransform>(childB)) {
        transform->parent       = _target;
        transform->anchorMin    = {0.0f, 0.0f};
        transform->anchorMax    = {1.0f, 1.0f};
        transform->offsetMin    = {0.0f, 0.0f};
        transform->offsetMax    = {0.0f, 0.0f};
        transform->clipChildren = true;
    }
    if (UiDockNode* childBNode = _scene->GetComponent<UiDockNode>(childB)) {
        childBNode->tabBar      = oldTabBar;
        childBNode->contentArea = oldContentArea;
        childBNode->windows     = oldWindows;
        childBNode->tabButtons  = std::move(oldTabButtons);
        childBNode->activeTab   = oldActiveTab;
        childBNode->tabsDirty   = true;
    }
    // 引き継いだタブバー/内容領域の親を新しい葉ノードへ付け替える。
    if (UiTransform* transform = _scene->GetComponent<UiTransform>(oldTabBar)) {
        transform->parent = childB;
    }
    if (UiTransform* transform = _scene->GetComponent<UiTransform>(oldContentArea)) {
        transform->parent = childB;
    }
    // 移った各ウィンドウの dockNode 参照を新しい葉ノードへ更新する。
    for (const EntityHandle& windowRoot : oldWindows) {
        if (UiWindow* window = _scene->GetComponent<UiWindow>(windowRoot)) {
            window->dockNode = childB;
        }
    }
    _runner->RegisterEntity<UiLayoutSystem, UiDockSystem>(childB);

    // --- スプリッター ---
    const EntityHandle splitter = CreateSplitterEntity(_scene, _runner, _target);

    // --- target 自身を分割ノードに作り替える ---
    // ここまでの間に childA/childB 用の新しいエンティティ/コンポーネントを何度も追加しており、
    // ComponentArray<UiDockNode> 内部の格納先が伸長 (再配置) された可能性があるため、
    // 冒頭で取得した targetNode を使い回さず、書き込み直前に取り直す (念のための安全策)。
    UiDockNode* freshTargetNode = _scene->GetComponent<UiDockNode>(_target);
    if (!freshTargetNode) {
        return {}; // 通常は起こらない経路。
    }
    freshTargetNode->split       = _split;
    freshTargetNode->splitRatio  = std::clamp(_ratio, 0.05f, 0.95f);
    freshTargetNode->childA      = childA;
    freshTargetNode->childB      = childB;
    freshTargetNode->splitter    = splitter;
    freshTargetNode->tabBar      = {};
    freshTargetNode->contentArea = {};
    freshTargetNode->activeTab   = 0;
    // windows/tabButtons は既に std::move 済みで空になっている。

    return childA;
}

void DockUiWindow(Scene* _scene, const EntityHandle& _window, const EntityHandle& _leafNode) {
    UiDockNode* node  = _scene->GetComponent<UiDockNode>(_leafNode);
    UiWindow* window  = _scene->GetComponent<UiWindow>(_window);
    if (!node || node->split != UiDockSplit::None || !window) {
        return; // 葉ノードでない、あるいはウィンドウ側の部品が欠けているなら何もしない。
    }

    // 既に他の葉ノードに入っていたら、まずそちらから抜く (ノード間の移動に対応する)。
    if (window->dockNode.IsValid() && window->dockNode != _leafNode) {
        RemoveWindowFromLeaf(_scene, window->dockNode, _window);
    }

    if (std::find(node->windows.begin(), node->windows.end(), _window) == node->windows.end()) {
        node->windows.push_back(_window);
        node->tabsDirty = true;
    }
    node->activeTab  = static_cast<int32_t>(node->windows.size()) - 1; // 追加したタブをアクティブにする。
    window->dockNode = _leafNode;

    // ウィンドウの見た目 (親子付け替え/全ストレッチ/表示状態/自作タイトルバーの非表示/
    // movable・resizable の無効化) は UiDockSystem が毎フレーム windows を見て設定する
    // (UiDockNode.h の「葉ノードのとき」を参照)。ここで一度だけ設定してしまうと、
    // タブの表示切り替え等ドックシステム側の継続的な責務と二重管理になるため行わない。
}

void UndockUiWindow(Scene* _scene, const EntityHandle& _window, const Vec2f& _position, const Vec2f& _size) {
    UiWindow* window             = _scene->GetComponent<UiWindow>(_window);
    UiTransform* windowTransform = _scene->GetComponent<UiTransform>(_window);
    if (!window || !windowTransform || !window->IsDocked()) {
        return; // 既にフローティングなら何もしない。
    }

    const EntityHandle leafNode = window->dockNode;
    RemoveWindowFromLeaf(_scene, leafNode, _window); // 葉ノードから抜き、空になれば畳む。

    window->dockNode  = {};
    window->movable   = true;
    window->resizable = true;

    // 自作タイトルバー/ボタンを再表示し、内容領域のオフセットを元に戻す (v10 の再結合と共通)。
    ShowUiWindowChrome(_scene, *window);

    // 点アンカー + 絶対ピクセルの矩形に戻す (v10 の再結合と同じ考え方)。
    windowTransform->parent    = {};
    windowTransform->anchorMin = {0.0f, 0.0f};
    windowTransform->anchorMax = {0.0f, 0.0f};
    windowTransform->offsetMin = _position;
    windowTransform->offsetMax = {_position[X] + _size[X], _position[Y] + _size[Y]};
}

} // namespace LogGuide
