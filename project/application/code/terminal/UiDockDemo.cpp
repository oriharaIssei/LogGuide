#include "UiDockDemo.h"

/// engine
#include "input/KeyboardInput.h"
#include "scene/Scene.h"
#include "system/SystemRunner.h"

/// application
#include "ui/UiDockBuilder.h"
#include "ui/UiWidgetBuilder.h"
#include "ui/component/UiDockNode.h"
#include "ui/system/UiWindowSystem.h"

using namespace OriGine;

namespace LogGuide {

namespace {
/// F4 でアンドックしたときに戻す位置/サイズ (確認用なので適当な値でよい)。
const Vec2f kUndockPosition = {80.0f, 420.0f};
const Vec2f kUndockSize     = {320.0f, 260.0f};
} // namespace

void UiDockDemo::Build(Scene* _scene, SystemRunner* _runner) {
    scene_  = _scene;
    runner_ = _runner;

    // メインサーフェス (0) を丸ごと覆うドックスペースを作る。
    dockSpace_ = CreateUiDockSpace(scene_, runner_, 0);

    // ダミーのウィンドウを 4 枚作る (位置/サイズはドックされるまでの仮の値。ドックした時点で
    // UiDockSystem が毎フレーム上書きする)。
    static const char* kTitles[4] = {"Dock A", "Dock B", "Dock C", "Dock D"};
    for (size_t i = 0; i < dummyWindows_.size(); ++i) {
        dummyWindows_[i] = CreateUiWindow(
            scene_, runner_, kTitles[i], {0.0f, 0.0f}, {320.0f, 260.0f}, static_cast<int32_t>(i));
    }

    // 中身の確認用に、ウィンドウ名だけ書いたラベルを 1 つ置く。
    static const char* kLabels[4] = {"Window A", "Window B", "Window C", "Window D"};
    for (size_t i = 0; i < dummyWindows_.size(); ++i) {
        UiWidgetDesc desc{};
        desc.parent    = dummyWindows_[i].contentArea;
        desc.anchorMin = {0.0f, 0.0f};
        desc.anchorMax = {1.0f, 1.0f};
        desc.offsetMin = {12.0f, 12.0f};
        desc.offsetMax = {-12.0f, -12.0f};
        desc.name      = "UiDockDemoLabel";
        CreateUiLabel(scene_, runner_, desc, kLabels[i], 16.0f);
    }

    // --- レイアウト: 左に 1 枚 (A) / 右上に 2 枚 (B, C をタブ) / 右下に 1 枚 (D) ---

    // 左 30% を新しい空の葉ノードとして切り出す (右側 70% に元の中身 (まだ何も無い) が残る)。
    const EntityHandle leftLeaf = SplitUiDockNode(scene_, runner_, dockSpace_, UiDockSplit::Horizontal, 0.3f);
    UiDockNode* rootNode        = scene_->GetComponent<UiDockNode>(dockSpace_);
    const EntityHandle rightLeaf = rootNode ? rootNode->childB : EntityHandle{};

    // 右側をさらに上 55% / 下 45% に分割する。
    const EntityHandle rightTopLeaf = SplitUiDockNode(scene_, runner_, rightLeaf, UiDockSplit::Vertical, 0.55f);
    UiDockNode* rightNode           = scene_->GetComponent<UiDockNode>(rightLeaf);
    const EntityHandle rightBottomLeaf = rightNode ? rightNode->childB : EntityHandle{};

    DockUiWindow(scene_, dummyWindows_[0].root, leftLeaf);        // 左: A 単独
    DockUiWindow(scene_, dummyWindows_[1].root, rightTopLeaf);    // 右上: B
    DockUiWindow(scene_, dummyWindows_[2].root, rightTopLeaf);    // 右上: C (B とタブになる)
    DockUiWindow(scene_, dummyWindows_[3].root, rightBottomLeaf); // 右下: D
}

void UiDockDemo::Update() {
    if (!scene_) {
        return;
    }

    // F4: 左に単独で入っている A をフローティングへ戻す。
    // 空になった葉ノード (leftLeaf) の親 (dockSpace_, 左右分割) が畳まれ、兄弟だった右側
    // (上下分割ノード) が dockSpace_ の位置へ繰り上がる (畳み処理の確認用)。
    if (KeyboardInput* keyboard = scene_->GetKeyboardInput()) {
        if (keyboard->IsTrigger(Key::F4)) {
            UndockUiWindow(scene_, dummyWindows_[0].root, kUndockPosition, kUndockSize);
            // ドック中は重なり順 (order/renderPriority) を触らない (UiDockSystem 参照) ため、
            // フローティングに戻した直後に前面へ出して他のウィンドウとの重なり順を確定させる
            // (v10 の再結合 (TerminalApp::HandleClosedSurfaces) と同じ後始末)。
            if (SystemRunner* runner = scene_->GetSystemRunnerRef()) {
                if (UiWindowSystem* windowSystem = runner->GetSystem<UiWindowSystem>()) {
                    windowSystem->BringWindowToFront(dummyWindows_[0].root);
                }
            }
        }
    }
}

std::vector<EntityHandle> UiDockDemo::GetWindowRoots() const {
    std::vector<EntityHandle> roots;
    roots.reserve(dummyWindows_.size());
    for (const UiWindowHandles& handles : dummyWindows_) {
        roots.push_back(handles.root);
    }
    return roots;
}

} // namespace LogGuide
