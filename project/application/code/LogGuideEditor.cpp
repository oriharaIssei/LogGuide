#include "LogGuideEditor.h"

#ifdef _DEBUG
#define ENGINE_INCLUDE
#define RESOURCE_DIRECTORY
#include <EngineInclude.h>

#include "globalVariables/GlobalVariables.h"
#include "scene/SceneManager.h"

#include "editor/EditorController.h"
#include "editor/sceneEditor/SceneEditor.h"
#include "editor/setting/SettingWindow.h"
#include "logger/Logger.h"

/// recording (ImGui 最小デモ)
#include "recording/RecordingPanel.h"
#include "recording/RecordingSystem.h"

/// player (2 動画同期再生デモ)
#include "player/DualPlayerController.h"
#include "player/FileDialog.h"
#include "player/PlayerPanel.h"

/// engine (window handle)
#include "winApp/WinApp.h"

using namespace OriGine;

LogGuideEditor::LogGuideEditor()  = default;
LogGuideEditor::~LogGuideEditor() = default;

void LogGuideEditor::Initialize(const std::vector<std::string>& _commandLines) {
    variables_    = GlobalVariables::GetInstance();
    engine_       = Engine::GetInstance();
    sceneManager_ = std::make_unique<SceneManager>();

    variables_->LoadAllFile();
    engine_->Initialize();

    (void)_commandLines;

    RegisterUsingComponents();
    RegisterUsingSystems();

    ApplyWindowSettings();

    EditorController::GetInstance()->AddEditor<SceneEditorWindow>(std::make_unique<SceneEditorWindow>());
    EditorController::GetInstance()->AddEditor<SettingWindow>(std::make_unique<SettingWindow>());
    EditorController::GetInstance()->Initialize();

    recordingSystem_ = std::make_unique<LogGuide::RecordingSystem>();
    recordingSystem_->Initialize(engine_);

    playerController_ = std::make_unique<LogGuide::DualPlayerController>();
    playerController_->Initialize();

    fileDrop_ = std::make_unique<LogGuide::WindowFileDrop>();
    fileDrop_->Initialize(engine_->GetWinApp()->GetHwnd());
}

void LogGuideEditor::Finalize() {
    fileDrop_.reset();
    playerController_.reset();
    recordingSystem_.reset();
    EditorController::GetInstance()->Finalize();
    sceneManager_.reset();
    engine_->Finalize();
}

void LogGuideEditor::Run() {
    while (!isEndRequest_) {
        if (engine_->ProcessMessage()) {
            isEndRequest_ = true;
            break;
        }

        engine_->BeginFrame();
        EditorController::GetInstance()->Update();
        LogGuide::DrawRecordingPanel(*recordingSystem_);
        playerController_->Update();
        LogGuide::DrawPlayerPanel(*playerController_, *fileDrop_, engine_->GetWinApp()->GetHwnd());
        engine_->EndFrame();

        engine_->ScreenPreDraw();
        engine_->ScreenPostDraw();
    }
}

#endif // _DEBUG
