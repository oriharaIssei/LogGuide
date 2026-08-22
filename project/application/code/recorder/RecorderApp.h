#pragma once

#include "FrameWork.h"

#include <memory>

namespace OriGine {
class SceneManager;
}

namespace LogGuide {
class RecordingSystem;
}

// 録画専用アプリケーション。
// 旧 LogGuideEditor から録画に関わる部分のみを取り出したもの。
// 再生系（DualPlayerController, WindowFileDrop）は持たない。
class RecorderApp : public FrameWork {
public:
    RecorderApp();
    ~RecorderApp() override;

    void Initialize(const std::vector<std::string>& _commandLines) override;
    void Finalize() override;
    void Run() override;

private:
    std::unique_ptr<OriGine::SceneManager>     sceneManager_    = nullptr;
    std::unique_ptr<LogGuide::RecordingSystem> recordingSystem_ = nullptr;
};
