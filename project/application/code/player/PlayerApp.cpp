#include "PlayerApp.h"

/// stl
#include <cctype>
#include <string>

#define ENGINE_INCLUDE
#define RESOURCE_DIRECTORY
#include <EngineInclude.h>

#include "globalVariables/GlobalVariables.h"
#include "scene/SceneManager.h"

/// engine (window handle)
#include "winApp/WinApp.h"
#include "logger/Logger.h"

/// module（全構成で有効）
#include "analysis/SummaryService.h"
#include "playback/DualPlayerController.h"
#include "playback/FileDialog.h"

#ifdef _DEBUG
/// editor（Debug 構成のみ存在する）
#include "editor/EditorController.h"
#include "editor/sceneEditor/SceneEditor.h"
#include "editor/setting/SettingWindow.h"

/// externals
#include <imgui/imgui.h>

/// player（要約ボタン付き UI。ImGui 同様 Debug 構成のみ存在する）
#include "playback/PlayerPanel.h"
#endif // _DEBUG

using namespace OriGine;

namespace {

// PlayerPanel のドラッグ&ドロップ処理と同じ判定。
// session.json ならセッション（camera/screen の 2 本）として、
// それ以外（mp4）なら単体動画としてスロット 0 に読み込む。
bool HasJsonExtension(const std::string& path) {
    if (path.size() < 5) {
        return false;
    }
    std::string tail = path.substr(path.size() - 5);
    for (auto& c : tail) {
        c = static_cast<char>(::tolower(static_cast<unsigned char>(c)));
    }
    return tail == ".json";
}

} // namespace

PlayerApp::PlayerApp()  = default;
PlayerApp::~PlayerApp() = default;

void PlayerApp::Initialize(const std::vector<std::string>& _commandLines) {
    variables_    = GlobalVariables::GetInstance();
    engine_       = Engine::GetInstance();
    sceneManager_ = std::make_unique<SceneManager>();

    variables_->LoadAllFile();
    engine_->Initialize();

    RegisterUsingComponents();
    RegisterUsingSystems();

    ApplyWindowSettings();

#ifdef _DEBUG
    // 録画アプリ（LogGuideRecorder）と作業ディレクトリを共有するため、
    // ウィンドウ配置の ini を分ける。
    ImGui::GetIO().IniFilename = "imgui_player.ini";

    EditorController::GetInstance()->AddEditor<SceneEditorWindow>(std::make_unique<SceneEditorWindow>());
    EditorController::GetInstance()->AddEditor<SettingWindow>(std::make_unique<SettingWindow>());
    EditorController::GetInstance()->Initialize();
#endif // _DEBUG

    // 録画アプリと見分けられるようウィンドウタイトルを変更する。
    engine_->GetWinApp()->SetWindowTitle(L"LogGuide Player");

    playerController_ = std::make_unique<LogGuide::DualPlayerController>();
    playerController_->Initialize();

    // ターミナル（LogGuideTerminal）から起動された場合、開くセッションが
    // 引数で渡される。[0] は exe 自身のパスなので実引数は [1] から。
    // 引数無しで起動されたときは何も開かず、UI から選ばせる。
    if (_commandLines.size() > 1 && !_commandLines[1].empty()) {
        const std::string& target = _commandLines[1];
        const bool opened = HasJsonExtension(target)
                                ? playerController_->OpenSession(target)
                                : playerController_->OpenSlot(0, target);
        if (opened) {
            LOG_INFO("PlayerApp: opened '{}' from command line", target);
        } else {
            LOG_WARN("PlayerApp: failed to open '{}': {}", target,
                     playerController_->GetLastError());
        }
    }

    fileDrop_ = std::make_unique<LogGuide::WindowFileDrop>();
    fileDrop_->Initialize(engine_->GetWinApp()->GetHwnd());

    // summarize モデルが無効/未ロードでも、要約ボタンが使えなくなるだけでアプリは続行する。
    summaryService_ = std::make_unique<LogGuide::SummaryService>();
    std::vector<std::string> summaryWarnings;
    if (!summaryService_->Initialize(&summaryWarnings)) {
        LOG_WARN("PlayerApp: summary service unavailable (summarize button disabled)");
    }
    // モデル欠落など、無効化された理由をログに残す（UI からは理由が見えないため）。
    for (const std::string& warning : summaryWarnings) {
        LOG_WARN("PlayerApp: {}", warning);
    }
}

void PlayerApp::Finalize() {
    summaryService_.reset();
    fileDrop_.reset();
    playerController_.reset();
#ifdef _DEBUG
    EditorController::GetInstance()->Finalize();
#endif // _DEBUG
    sceneManager_.reset();
    engine_->Finalize();
}

void PlayerApp::Run() {
    while (!isEndRequest_) {
        if (engine_->ProcessMessage()) {
            isEndRequest_ = true;
            break;
        }

        engine_->BeginFrame();
#ifdef _DEBUG
        EditorController::GetInstance()->Update();
#endif // _DEBUG
        playerController_->Update();
#ifdef _DEBUG
        LogGuide::DrawPlayerPanel(*playerController_, *fileDrop_, engine_->GetWinApp()->GetHwnd(),
                                  summaryService_.get());
#endif // _DEBUG
        engine_->EndFrame();

        engine_->ScreenPreDraw();
        engine_->ScreenPostDraw();
    }
}
