#pragma once

#include "FrameWork.h"

#include <memory>
#include <string>

namespace OriGine {
class SceneManager;
}

namespace LogGuide {
class SessionCatalog;
}

// =============================================================================
// TerminalApp
//
// LogGuideTerminal.exe のアプリケーション本体。録画系（RecordingSystem）も
// 再生系（DualPlayerController）も持たない、純粋なランチャー。
// ImGui の UI からレコーダー/プレイヤーの exe を起動したら自身は終了する
// （常駐しない）。EditorController（シーンエディタ）はランチャーに不要なため
// 初期化しない。
// Debug/Develop/Release いずれの構成でもコンパイル・リンクできる（UI は
// ImGui に依存するため Debug 構成のみ表示される。他構成では何もせず終了する）。
// =============================================================================
class TerminalApp : public FrameWork {
public:
    TerminalApp();
    ~TerminalApp() override;

    void Initialize(const std::vector<std::string>& _commandLines) override;
    void Finalize() override;
    void Run() override;

private:
    std::unique_ptr<OriGine::SceneManager>    sceneManager_ = nullptr;
    std::unique_ptr<LogGuide::SessionCatalog> catalog_      = nullptr;
    std::string                               lastError_;
};
