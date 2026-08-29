#pragma once

#include "entity/EntityHandle.h"
#include "ui/UiWindowBuilder.h"

#include <array>
#include <vector>

namespace OriGine {
class Scene;
class SystemRunner;
} // namespace OriGine

namespace LogGuide {

// =============================================================================
// UiDockDemo
//
// v14 (ドックツリーとタブ) の確認用デモ。TerminalLauncherUi (v13 のランチャー実 UI) とは
// 独立に、メインサーフェスへドックスペースと 4 枚のダミーウィンドウを組み立てる。
//
// レイアウトは「左に 1 枚 / 右上に 2 枚 (タブ) / 右下に 1 枚」。
// 仕様書は「ダミーのウィンドウを 3 枚作り…」としているが、「左右上下 3 リーフそれぞれに
// 中身がある」状態と「どれか 1 つの葉ノードに 2 枚入れてタブにする」状態を 1 つのウィンドウの
// 使い回し無しで同時に満たすには、リーフの数 (3) より 1 枚多いウィンドウが要る (同じウィンドウを
// 2 つの葉ノードに同時に置くことはできない)。そのため確認用として 4 枚 (A/B/C/D) 作り、
// B と C を右上の葉ノードでタブにしている。
//
// F4 キーで先頭のウィンドウ (左に単独で入っている A) をフローティングへ戻す
// (UndockUiWindow / 空になった葉ノードの畳みの確認用)。
// =============================================================================
class UiDockDemo {
public:
    /// ドックスペースと 4 枚のダミーウィンドウを組み立てる.
    void Build(OriGine::Scene* _scene, OriGine::SystemRunner* _runner);

    /// 1 フレーム分の更新 (F4 キーでのアンドック確認).
    void Update();

    /// ダミーウィンドウ 4 枚のルート一覧 (アプリの v10 切り離し/再結合対象へ追加するため).
    std::vector<OriGine::EntityHandle> GetWindowRoots() const;

private:
    OriGine::Scene* scene_         = nullptr;
    OriGine::SystemRunner* runner_ = nullptr;

    OriGine::EntityHandle dockSpace_{};
    /// ダミーウィンドウ 4 枚 (A/B/C/D の順)。F4 で [0] (A) だけをアンドックする。
    std::array<UiWindowHandles, 4> dummyWindows_{};
};

} // namespace LogGuide
