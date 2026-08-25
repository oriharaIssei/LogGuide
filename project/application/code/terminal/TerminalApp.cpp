#include "TerminalApp.h"

#define ENGINE_INCLUDE
#define RESOURCE_DIRECTORY
#include <EngineInclude.h>

#include "globalVariables/GlobalVariables.h"
#include "scene/SceneManager.h"

/// engine (window handle)
#include "winApp/WinApp.h"
#include "logger/Logger.h"

/// module（全構成で有効）
#include "SessionCatalog.h"

#ifdef _DEBUG
/// externals
#include <imgui/imgui.h>

/// terminal（ランチャー UI。ImGui 同様 Debug 構成のみ存在する）
#include "TerminalPanel.h"
#endif // _DEBUG

using namespace OriGine;

TerminalApp::TerminalApp()  = default;
TerminalApp::~TerminalApp() = default;

void TerminalApp::Initialize(const std::vector<std::string>& _commandLines) {
    variables_    = GlobalVariables::GetInstance();
    engine_       = Engine::GetInstance();
    sceneManager_ = std::make_unique<SceneManager>();

    variables_->LoadAllFile();
    engine_->Initialize();

    (void)_commandLines; // ターミナルは引数を取らない

    RegisterUsingComponents();
    RegisterUsingSystems();

    ApplyWindowSettings();

#ifdef _DEBUG
    // 録画/再生アプリと作業ディレクトリを共有するため、ウィンドウ配置の ini を分ける。
    // ランチャーにシーンエディタは不要なので、EditorController は初期化しない。
    ImGui::GetIO().IniFilename = "imgui_terminal.ini";
#endif // _DEBUG

    // 他アプリと見分けられるようウィンドウタイトルを設定する。
    engine_->GetWinApp()->SetWindowTitle(L"LogGuide");

    // 録画セッション一覧を起動時に一度読み込んでおく。
    catalog_ = std::make_unique<LogGuide::SessionCatalog>();
    catalog_->Refresh();
}

void TerminalApp::Finalize() {
    catalog_.reset();
    // EditorController は Initialize していないため Finalize も呼ばない。
    sceneManager_.reset();
    engine_->Finalize();
}

void TerminalApp::Run() {
#ifdef _DEBUG
    while (!isEndRequest_) {
        if (engine_->ProcessMessage()) {
            isEndRequest_ = true;
            break;
        }

        engine_->BeginFrame();
        if (LogGuide::DrawTerminalPanel(*catalog_, &lastError_)) {
            isEndRequest_ = true; // アプリを起動したのでターミナルは終了する
        }
        engine_->EndFrame();

        engine_->ScreenPreDraw();
        engine_->ScreenPostDraw();
    }
#else
    // ImGui はエンジン側で Debug 構成限定のため、このビルドではランチャー UI を出せない。
    // 操作不能な空ウィンドウを残さないよう、ループに入らず何もせずに終了する。
    // （ループを #ifdef の外に置くと Release で到達不能コードになり C4702 が出る）
    LOG_WARN("TerminalApp: launcher UI requires a Debug build; exiting");
#endif // _DEBUG
}
