#include "RecorderApp.h"

/// engine
#define ENGINE_INCLUDE
#define RESOURCE_DIRECTORY
#include <EngineInclude.h>

#include "globalVariables/GlobalVariables.h"
#include "scene/SceneManager.h"
#include "winApp/WinApp.h"

/// recording
#include "recording/RecordingSystem.h"

#ifdef _DEBUG
#include "editor/EditorController.h"
#include "editor/sceneEditor/SceneEditor.h"
#include "editor/setting/SettingWindow.h"

/// recording panel (ImGui 最小デモ)
#include "recording/RecordingPanel.h"
#include <imgui/imgui.h>
#endif // _DEBUG

using namespace OriGine;

RecorderApp::RecorderApp()  = default;
RecorderApp::~RecorderApp() = default;

void RecorderApp::Initialize(const std::vector<std::string>& _commandLines) {
    variables_    = GlobalVariables::GetInstance();
    engine_       = Engine::GetInstance();
    sceneManager_ = std::make_unique<SceneManager>();

    variables_->LoadAllFile();
    engine_->Initialize();

    (void)_commandLines;

    RegisterUsingComponents();
    RegisterUsingSystems();

    ApplyWindowSettings();

#ifdef _DEBUG
    // 再生アプリと作業ディレクトリを共有するため、ウィンドウ配置の ini を分ける。
    ImGui::GetIO().IniFilename = "imgui_recorder.ini";

    EditorController::GetInstance()->AddEditor<SceneEditorWindow>(std::make_unique<SceneEditorWindow>());
    EditorController::GetInstance()->AddEditor<SettingWindow>(std::make_unique<SettingWindow>());
    EditorController::GetInstance()->Initialize();
#endif // _DEBUG

    // 再生アプリと見分けられるようウィンドウタイトルを変更する。
    engine_->GetWinApp()->SetWindowTitle(L"LogGuide Recorder");

    recordingSystem_ = std::make_unique<LogGuide::RecordingSystem>();
    recordingSystem_->Initialize(engine_);
}

void RecorderApp::Finalize() {
    // RecordingSystem のデストラクタが内部で Finalize（録画中なら停止）を行う。
    recordingSystem_.reset();

#ifdef _DEBUG
    EditorController::GetInstance()->Finalize();
#endif // _DEBUG

    sceneManager_.reset();
    engine_->Finalize();
}

void RecorderApp::Run() {
    while (!isEndRequest_) {
        if (engine_->ProcessMessage()) {
            isEndRequest_ = true;
            break;
        }

        engine_->BeginFrame();
#ifdef _DEBUG
        EditorController::GetInstance()->Update();
        LogGuide::DrawRecordingPanel(*recordingSystem_);
#endif // _DEBUG
        engine_->EndFrame();

        engine_->ScreenPreDraw();
        engine_->ScreenPostDraw();
    }
}
