#pragma once

#include "FrameWork.h"

#include <cstdint>
#include <memory>
#include <string>

// panel_ (EntityHandle) をメンバとして値で持つために定義が要る。Scene はポインタで
// 持つだけなので前方宣言のままでよい。
#include "entity/EntityHandle.h"

namespace OriGine {
class SceneManager;
class Scene;
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
// Debug/Develop/Release いずれの構成でもコンパイル・リンクでき、ImGui に依存しない
// 自作 UI（ECS 上のコンポーネント/システムとして実装）をすべての構成で描画する。
// ImGui によるランチャー UI（TerminalPanel）は Debug 構成のみ表示される。
// =============================================================================
class TerminalApp : public FrameWork {
public:
    TerminalApp();
    ~TerminalApp() override;

    void Initialize(const std::vector<std::string>& _commandLines) override;
    void Finalize() override;
    void Run() override;

private:
    /// <summary>
    /// 1 フレーム分の更新と描画.
    /// Run() のループからだけでなく、ウィンドウのサイズ変更/移動のモーダルループ中に
    /// WinApp からも呼ばれる（そうしないとドラッグ中に 1 フレームも描画されない）。
    /// </summary>
    void Frame();

    std::unique_ptr<OriGine::SceneManager>    sceneManager_ = nullptr;
    std::unique_ptr<LogGuide::SessionCatalog> catalog_      = nullptr;
    std::unique_ptr<OriGine::Scene>           scene_        = nullptr; // ECS 上の自作 UI (角丸矩形) を描画するためのシーン
    OriGine::EntityHandle                     panel_{};                // 動作確認用パネル。クリック回数表示のため Run() から参照する
    OriGine::EntityHandle                     childBar_{};             // v4: 階層/クリップ確認用の子要素。親からはみ出す帯。Run() からは参照しない
    int32_t                                   clickCount_   = 0;
    std::string                               lastError_;
};
