#include "TerminalLauncherUi.h"

/// module
#include "AppLauncher.h"
#include "SessionCatalog.h"

/// engine
#include "component/text/TextComponent.h"
#include "scene/Scene.h"
#include "system/SystemRunner.h"

/// application (自作 UI コンポーネント)
#include "ui/component/UiInteractable.h"
#include "ui/component/UiScrollView.h"
#include "ui/component/UiTransform.h"

/// stl
#include <cstdio>

using namespace OriGine;

namespace LogGuide {

namespace {

// --- レイアウト定数 ---

/// 一覧の行の高さ / 行間 (px).
constexpr float kRowHeight  = 28.0f;
constexpr float kRowSpacing = 2.0f;

/// カラムの x オフセット (行の中でラベルを横に並べるための境界。ヘッダ行と一覧の行で共通)。
constexpr float kColumnSessionX0  = 0.0f;
constexpr float kColumnStartedX0  = 300.0f;
constexpr float kColumnDurationX0 = 480.0f;
constexpr float kColumnTrackX0    = 560.0f;
constexpr float kColumnTrackX1    = 640.0f;

/// エラー行の文字色 (赤系).
constexpr Vec4f kErrorColor = {1.0f, 0.4f, 0.4f, 1.0f};

// セッション尺の表示用フォーマット（m:ss）。
// terminal/TerminalPanel.cpp の FormatDuration と同じ整形 (四捨五入して "%d:%02d")。
// あちらは無名名前空間の中にあり共有できないため、同じものをここにも書く。
std::string FormatDuration(double _seconds) {
    if (_seconds < 0.0) {
        _seconds = 0.0;
    }
    const int totalSec = static_cast<int>(_seconds + 0.5); // 四捨五入
    char buf[16]        = {};
    std::snprintf(buf, sizeof(buf), "%d:%02d", totalSec / 60, totalSec % 60);
    return buf;
}

} // namespace

void TerminalLauncherUi::Build(Scene* _scene, SystemRunner* _runner, SessionCatalog* _catalog) {
    scene_   = _scene;
    runner_  = _runner;
    catalog_ = _catalog;

    window_ = CreateUiWindow(scene_, runner_, "LogGuide", {60.0f, 60.0f}, {720.0f, 520.0f}, 0);

    // --- レコーダーを起動 ---
    UiWidgetDesc recorderDesc{};
    recorderDesc.parent    = window_.contentArea;
    recorderDesc.anchorMin = {0.0f, 0.0f};
    recorderDesc.anchorMax = {0.0f, 0.0f};
    recorderDesc.offsetMin = {16.0f, 16.0f};
    recorderDesc.offsetMax = {236.0f, 52.0f};
    recorderDesc.name      = "UiLauncherRecorderButton";
    recorderButton_        = CreateUiButton(scene_, runner_, recorderDesc, "レコーダーを起動");

    // --- 区切り線 ---
    UiWidgetDesc separatorDesc{};
    separatorDesc.parent    = window_.contentArea;
    separatorDesc.anchorMin = {0.0f, 0.0f};
    separatorDesc.anchorMax = {1.0f, 0.0f};
    separatorDesc.offsetMin = {16.0f, 64.0f};
    separatorDesc.offsetMax = {-16.0f, 66.0f};
    separatorDesc.name      = "UiLauncherSeparator";
    CreateUiPanel(scene_, runner_, separatorDesc, {0.32f, 0.34f, 0.40f, 1.0f});

    // --- "録画セッション" ラベル + "更新" ボタン ---
    UiWidgetDesc sessionsLabelDesc{};
    sessionsLabelDesc.parent    = window_.contentArea;
    sessionsLabelDesc.anchorMin = {0.0f, 0.0f};
    sessionsLabelDesc.anchorMax = {0.0f, 0.0f};
    sessionsLabelDesc.offsetMin = {16.0f, 76.0f};
    sessionsLabelDesc.offsetMax = {300.0f, 104.0f};
    sessionsLabelDesc.name      = "UiLauncherSessionsLabel";
    CreateUiLabel(scene_, runner_, sessionsLabelDesc, "録画セッション");

    UiWidgetDesc refreshDesc{};
    refreshDesc.parent    = window_.contentArea;
    refreshDesc.anchorMin = {1.0f, 0.0f};
    refreshDesc.anchorMax = {1.0f, 0.0f};
    refreshDesc.offsetMin = {-116.0f, 76.0f};
    refreshDesc.offsetMax = {-16.0f, 104.0f};
    refreshDesc.name      = "UiLauncherRefreshButton";
    refreshButton_        = CreateUiButton(scene_, runner_, refreshDesc, "更新", 15.0f);

    // --- ヘッダ行 (ラベル 4 つ. スクロールしない) ---
    auto createHeaderLabel = [this](float _x0, float _x1, const std::string& _text) {
        UiWidgetDesc desc{};
        desc.parent    = window_.contentArea;
        desc.anchorMin = {0.0f, 0.0f};
        desc.anchorMax = {0.0f, 0.0f};
        desc.offsetMin = {16.0f + _x0, 112.0f};
        desc.offsetMax = {16.0f + _x1, 138.0f};
        desc.name      = "UiLauncherHeaderLabel";
        CreateUiLabel(scene_, runner_, desc, _text, 14.0f);
    };
    createHeaderLabel(kColumnSessionX0, kColumnStartedX0, "セッション");
    createHeaderLabel(kColumnStartedX0, kColumnDurationX0, "開始時刻");
    createHeaderLabel(kColumnDurationX0, kColumnTrackX0, "尺");
    createHeaderLabel(kColumnTrackX0, kColumnTrackX1, "トラック");

    // --- セッション一覧 (縦スクロール) ---
    UiWidgetDesc scrollDesc{};
    scrollDesc.parent    = window_.contentArea;
    scrollDesc.anchorMin = {0.0f, 0.0f};
    scrollDesc.anchorMax = {1.0f, 1.0f};
    scrollDesc.offsetMin = {16.0f, 146.0f};
    scrollDesc.offsetMax = {-16.0f, -88.0f}; // 下は固定要素 (エラー行 + ボタン行) の分だけ余白を空ける
    scrollDesc.name      = "UiLauncherSessionScrollView";
    sessionScrollView_   = CreateUiScrollView(scene_, runner_, scrollDesc);

    // --- エラー行 (無いときは非表示) ---
    UiWidgetDesc errorDesc{};
    errorDesc.parent    = window_.contentArea;
    errorDesc.anchorMin = {0.0f, 1.0f};
    errorDesc.anchorMax = {1.0f, 1.0f};
    errorDesc.offsetMin = {16.0f, -80.0f};
    errorDesc.offsetMax = {-16.0f, -60.0f};
    errorDesc.name      = "UiLauncherErrorLabel";
    errorLabel_          = CreateUiLabel(scene_, runner_, errorDesc, "", 14.0f);
    if (UiTransform* errorTransform = scene_->GetComponent<UiTransform>(errorLabel_)) {
        errorTransform->visible = false; // 初期状態はエラー無し
    }

    // --- 下段のボタン (下寄せ) ---
    UiWidgetDesc playSelectedDesc{};
    playSelectedDesc.parent    = window_.contentArea;
    playSelectedDesc.anchorMin = {0.0f, 1.0f};
    playSelectedDesc.anchorMax = {0.0f, 1.0f};
    playSelectedDesc.offsetMin = {16.0f, -52.0f};
    playSelectedDesc.offsetMax = {276.0f, -16.0f};
    playSelectedDesc.name      = "UiLauncherPlaySelectedButton";
    playSelectedButton_        = CreateUiButton(scene_, runner_, playSelectedDesc, "選択したセッションを再生", 15.0f);

    UiWidgetDesc playEmptyDesc{};
    playEmptyDesc.parent    = window_.contentArea;
    playEmptyDesc.anchorMin = {0.0f, 1.0f};
    playEmptyDesc.anchorMax = {0.0f, 1.0f};
    playEmptyDesc.offsetMin = {292.0f, -52.0f};
    playEmptyDesc.offsetMax = {592.0f, -16.0f};
    playEmptyDesc.name      = "UiLauncherPlayWithoutSessionButton";
    playEmptyButton_        = CreateUiButton(scene_, runner_, playEmptyDesc, "セッションを選ばずにプレイヤーを起動", 14.0f);

    // 起動時点のカタログ内容 (TerminalApp::Initialize が既に Refresh() 済み) で一覧を作る。
    RebuildSessionRows();
}

bool TerminalLauncherUi::Update(std::string* _outError) {
    bool launched = false;

    // --- レコーダーを起動 ---
    if (UiInteractable* interactable = scene_->GetComponent<UiInteractable>(recorderButton_)) {
        if (interactable->wasClicked) {
            std::string err;
            if (LaunchRecorder(&err)) {
                launched = true;
            } else if (_outError != nullptr) {
                *_outError = err;
            }
        }
    }

    // --- 更新 ---
    if (UiInteractable* interactable = scene_->GetComponent<UiInteractable>(refreshButton_)) {
        if (interactable->wasClicked) {
            catalog_->Refresh();
            RebuildSessionRows();
        }
    }

    const std::vector<SessionEntry>& entries = catalog_->Entries();

    // --- 一覧の行のクリック (排他選択) ---
    for (size_t i = 0; i < sessionRows_.size(); ++i) {
        UiInteractable* interactable = scene_->GetComponent<UiInteractable>(sessionRows_[i].button);
        if (interactable != nullptr && interactable->wasClicked) {
            selectedIndex_ = static_cast<int32_t>(i);
            break;
        }
    }
    for (size_t i = 0; i < sessionRows_.size(); ++i) {
        if (UiInteractable* interactable = scene_->GetComponent<UiInteractable>(sessionRows_[i].button)) {
            interactable->isSelected = (static_cast<int32_t>(i) == selectedIndex_);
        }
    }

    const bool hasValidSelection =
        (selectedIndex_ >= 0) &&
        (static_cast<size_t>(selectedIndex_) < entries.size()) &&
        entries[static_cast<size_t>(selectedIndex_)].manifestValid;

    // --- 選択したセッションを再生 ---
    // 有効な選択が無いときは enabled = false にする (ここで毎フレーム設定する)。
    if (UiInteractable* interactable = scene_->GetComponent<UiInteractable>(playSelectedButton_)) {
        interactable->enabled = hasValidSelection;
        if (hasValidSelection && interactable->wasClicked) {
            std::string err;
            if (LaunchPlayer(entries[static_cast<size_t>(selectedIndex_)].manifestPath, &err)) {
                launched = true;
            } else if (_outError != nullptr) {
                *_outError = err;
            }
        }
    }

    // --- セッションを選ばずにプレイヤーを起動 ---
    if (UiInteractable* interactable = scene_->GetComponent<UiInteractable>(playEmptyButton_)) {
        if (interactable->wasClicked) {
            std::string err;
            if (LaunchPlayer("", &err)) {
                launched = true;
            } else if (_outError != nullptr) {
                *_outError = err;
            }
        }
    }

    UpdateErrorLabel(_outError);

    return launched;
}

EntityHandle TerminalLauncherUi::GetWindowRoot() const {
    return window_.root;
}

void TerminalLauncherUi::RebuildSessionRows() {
    // 前回作った行 (土台ボタン + 4 ラベル) を必ず破棄してから作り直す (リークさせない)。
    for (const SessionRow& row : sessionRows_) {
        scene_->AddDeleteEntity(row.button);
        scene_->AddDeleteEntity(row.sessionLabel);
        scene_->AddDeleteEntity(row.startedLabel);
        scene_->AddDeleteEntity(row.durationLabel);
        scene_->AddDeleteEntity(row.trackLabel);
    }
    sessionRows_.clear();

    if (emptyListLabel_.IsValid()) {
        scene_->AddDeleteEntity(emptyListLabel_);
        emptyListLabel_ = {};
    }

    // 作り直しで選択インデックスは無効になる (ImGui 版の selected >= entries.size() と同じ扱い)。
    selectedIndex_ = -1;

    const std::vector<SessionEntry>& entries = catalog_->Entries();

    if (entries.empty()) {
        CreateEmptyListLabel();
        if (UiScrollView* scrollView = scene_->GetComponent<UiScrollView>(sessionScrollView_.viewport)) {
            scrollView->contentHeight = 0.0f;
            scrollView->scrollOffset  = 0.0f;
        }
        return;
    }

    sessionRows_.reserve(entries.size());
    for (size_t i = 0; i < entries.size(); ++i) {
        const SessionEntry& entry = entries[i];
        const float y0            = static_cast<float>(i) * (kRowHeight + kRowSpacing);
        const float y1            = y0 + kRowHeight;

        UiWidgetDesc rowDesc{};
        rowDesc.parent    = sessionScrollView_.content;
        rowDesc.anchorMin = {0.0f, 0.0f};
        rowDesc.anchorMax = {1.0f, 0.0f};
        rowDesc.offsetMin = {0.0f, y0};
        rowDesc.offsetMax = {0.0f, y1};
        rowDesc.name      = "UiLauncherSessionRow";

        SessionRow row{};
        row.button = CreateUiButton(scene_, runner_, rowDesc, ""); // 中身は列ラベルで表すので土台は無地

        // session.json を読めない行は選択不可にする (UiHighlightSystem が disabledColor にする)。
        if (UiInteractable* interactable = scene_->GetComponent<UiInteractable>(row.button)) {
            interactable->enabled = entry.manifestValid;
        }

        const std::string sessionText =
            entry.manifestValid ? entry.sessionId : (entry.sessionId + " (session.json を読めません)");
        const std::string startedText =
            (!entry.manifestValid || entry.startedAtIso.empty()) ? "-" : entry.startedAtIso;
        const std::string durationText = entry.manifestValid ? FormatDuration(entry.durationSeconds) : "-";
        const std::string trackText    = entry.manifestValid ? std::to_string(entry.trackCount) : "-";

        auto createColumnLabel = [this, &row](float _x0, float _x1, const std::string& _text) {
            UiWidgetDesc desc{};
            desc.parent         = row.button;
            desc.anchorMin      = {0.0f, 0.0f};
            desc.anchorMax      = {0.0f, 1.0f};
            desc.offsetMin      = {_x0, 0.0f};
            desc.offsetMax      = {_x1, 0.0f};
            desc.renderPriority = 1; // 行の背景 (土台ボタン) より手前に描く
            desc.name           = "UiLauncherSessionRowLabel";
            return CreateUiLabel(scene_, runner_, desc, _text, 14.0f);
        };

        row.sessionLabel  = createColumnLabel(kColumnSessionX0, kColumnStartedX0, sessionText);
        row.startedLabel  = createColumnLabel(kColumnStartedX0, kColumnDurationX0, startedText);
        row.durationLabel = createColumnLabel(kColumnDurationX0, kColumnTrackX0, durationText);
        row.trackLabel    = createColumnLabel(kColumnTrackX0, kColumnTrackX1, trackText);

        sessionRows_.push_back(row);
    }

    // contentHeight は子孫を辿って自動計算しない (UiScrollView.h の方針通り) ので、
    // 「行数 × (行の高さ + 行間) - 最後の行間」で単純に出す。
    if (UiScrollView* scrollView = scene_->GetComponent<UiScrollView>(sessionScrollView_.viewport)) {
        scrollView->contentHeight =
            static_cast<float>(entries.size()) * (kRowHeight + kRowSpacing) - kRowSpacing;
        scrollView->scrollOffset = 0.0f;
    }
}

void TerminalLauncherUi::CreateEmptyListLabel() {
    UiWidgetDesc desc{};
    desc.parent    = sessionScrollView_.content;
    desc.anchorMin = {0.0f, 0.0f};
    desc.anchorMax = {1.0f, 0.0f};
    desc.offsetMin = {8.0f, 8.0f};
    desc.offsetMax = {-8.0f, 8.0f + kRowHeight};
    desc.name      = "UiLauncherEmptyListLabel";
    emptyListLabel_ = CreateUiLabel(
        scene_, runner_, desc, "録画セッションがありません。先にレコーダーで録画してください。", 14.0f);
}

void TerminalLauncherUi::UpdateErrorLabel(const std::string* _outError) {
    std::string message;
    if (!catalog_->LastError().empty()) {
        message = catalog_->LastError();
    } else if (_outError != nullptr && !_outError->empty()) {
        message = *_outError;
    }

    if (message == lastDisplayedError_) {
        return; // 変化なし。無駄な dirty 立てを避ける
    }
    lastDisplayedError_ = message;

    if (UiTransform* transform = scene_->GetComponent<UiTransform>(errorLabel_)) {
        transform->visible = !message.empty();
    }
    if (TextComponent* text = scene_->GetComponent<TextComponent>(errorLabel_)) {
        text->text  = message.empty() ? "" : ("Error: " + message);
        text->color = kErrorColor;
        text->dirty = true; // 文字列を書き換えたら自分で立てる
    }
}

} // namespace LogGuide
