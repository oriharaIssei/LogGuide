#pragma once

#include "entity/EntityHandle.h"
#include "ui/UiWidgetBuilder.h"
#include "ui/UiWindowBuilder.h"

#include <cstdint>
#include <string>
#include <vector>

namespace OriGine {
class Scene;
class SystemRunner;
} // namespace OriGine

namespace LogGuide {

class SessionCatalog;

// =============================================================================
// TerminalLauncherUi
//
// 自作 UI (ECS + SDF シェーダ) によるランチャー画面。
// ImGui 版 (TerminalPanel) と同じことを、ImGui に依存せず全構成 (Debug/Develop/Release) で行う。
// v13 の到達点: これまでウィンドウ A / B に入っていた動作確認用デモを実 UI に置き換える。
// =============================================================================
class TerminalLauncherUi {
public:
    /// ウィンドウと中身を組み立てる. カタログは参照を持ち続けるので寿命に注意.
    void Build(OriGine::Scene* _scene, OriGine::SystemRunner* _runner, SessionCatalog* _catalog);

    /// 1 フレーム分の更新. アプリを起動したら true を返す (呼び出し側はターミナルを終了させる).
    /// 起動に失敗したら false を返し、_outError に理由を入れる (成功時は _outError を書き換えない).
    bool Update(std::string* _outError);

    /// ウィンドウのルート (切り離し/再結合の対象としてアプリが覚えておくため).
    OriGine::EntityHandle GetWindowRoot() const;

private:
    /// 一覧の 1 行分のエンティティ (土台のボタン + 4 カラムのラベル).
    struct SessionRow {
        OriGine::EntityHandle button{};        ///< 土台 (背景 + 当たり判定 + ハイライト)
        OriGine::EntityHandle sessionLabel{};
        OriGine::EntityHandle startedLabel{};
        OriGine::EntityHandle durationLabel{};
        OriGine::EntityHandle trackLabel{};
    };

    /// catalog_->Entries() から一覧の行を作り直す.
    /// 前回作った行のエンティティは必ず破棄してから作り直す (リークさせない).
    /// 呼び出し側 (更新ボタンのハンドラ) が必要なら Refresh() を先に済ませておくこと.
    void RebuildSessionRows();
    /// 一覧が空のときに出す案内ラベルを 1 つ作る.
    void CreateEmptyListLabel();
    /// エラー行の表示内容 (テキスト/表示・非表示) を更新する.
    void UpdateErrorLabel(const std::string* _outError);

    OriGine::Scene* scene_         = nullptr;
    OriGine::SystemRunner* runner_ = nullptr;
    SessionCatalog* catalog_       = nullptr;

    UiWindowHandles window_{};

    OriGine::EntityHandle recorderButton_{};
    OriGine::EntityHandle refreshButton_{};
    OriGine::EntityHandle playSelectedButton_{};
    OriGine::EntityHandle playEmptyButton_{};
    OriGine::EntityHandle errorLabel_{};

    UiScrollViewHandles sessionScrollView_{};
    std::vector<SessionRow> sessionRows_;
    /// 一覧が空のときだけ有効 (行の代わりに 1 つだけ出す案内ラベル).
    OriGine::EntityHandle emptyListLabel_{};

    /// 選択中の行 (sessionRows_ のインデックス). 選択無しは -1.
    int32_t selectedIndex_ = -1;

    /// エラー行に直近表示した内容 (変化したときだけ書き換えるための比較用).
    std::string lastDisplayedError_;
};

} // namespace LogGuide
