#pragma once

#include "FrameWork.h"

#include <memory>

namespace OriGine {
class SceneManager;
}

namespace LogGuide {
class RecordingSystem;
class DualPlayerController;
class WindowFileDrop;
}

class LogGuideEditor : public FrameWork {
public:
    LogGuideEditor();
    ~LogGuideEditor() override;

    void Initialize(const std::vector<std::string>& _commandLines) override;
    void Finalize() override;
    void Run() override;

private:
    std::unique_ptr<OriGine::SceneManager>        sceneManager_     = nullptr;
    std::unique_ptr<LogGuide::RecordingSystem>    recordingSystem_  = nullptr;
    std::unique_ptr<LogGuide::DualPlayerController> playerController_ = nullptr;
    std::unique_ptr<LogGuide::WindowFileDrop>     fileDrop_         = nullptr;
};
