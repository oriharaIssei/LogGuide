#pragma once

#include "FrameWork.h"

#include <memory>

namespace OriGine {
class SceneManager;
}

namespace LogGuide {
class DualPlayerController;
class WindowFileDrop;
class SummaryService;
}

// =============================================================================
// PlayerApp
//
// LogGuidePlayer.exe のアプリケーション本体。録画系（RecordingSystem 等）は
// 持たず、2 動画同期再生と（LLM が使えれば）要約生成のみを担う。
// Debug/Develop/Release いずれの構成でもコンパイル・リンクできる（UI は
// ImGui / EditorController に依存するため Debug 構成のみ表示される）。
// =============================================================================
class PlayerApp : public FrameWork {
public:
    PlayerApp();
    ~PlayerApp() override;

    void Initialize(const std::vector<std::string>& _commandLines) override;
    void Finalize() override;
    void Run() override;

private:
    std::unique_ptr<OriGine::SceneManager>          sceneManager_     = nullptr;
    std::unique_ptr<LogGuide::DualPlayerController> playerController_ = nullptr;
    std::unique_ptr<LogGuide::WindowFileDrop>       fileDrop_         = nullptr;
    std::unique_ptr<LogGuide::SummaryService>       summaryService_   = nullptr;
};
