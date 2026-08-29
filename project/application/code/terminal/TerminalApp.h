#pragma once

#include "FrameWork.h"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

// uiWindowRoots_ / detachedWindowsBySurface_ (EntityHandle) をメンバとして値で持つために定義が要る。
// Scene はポインタで持つだけなので前方宣言のままでよい。
#include "entity/EntityHandle.h"

// nativeWindows_ (unique_ptr) をメンバとして値で持つために定義が要る。
#include "ui/native/NativeWindowManager.h"

// v13: launcherUi_ (値メンバ) の定義が要る。ランチャーの実 UI 一式 (ウィンドウ組み立て/更新) は
// ここにまとめてある (TerminalApp.cpp が肥大化しないようにするため)。
#include "TerminalLauncherUi.h"
// v14: dockDemo_ (値メンバ) の定義が要る。ドックツリー/タブの確認用デモ一式。
#include "UiDockDemo.h"

// v10: 切り離し/再結合の座標計算に使う。
#include <Vector2.h>

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
// launcherUi_（自作 UI）からレコーダー/プレイヤーの exe を起動したら自身は終了する
// （常駐しない）。EditorController（シーンエディタ）はランチャーに不要なため
// 初期化しない。
// Debug/Develop/Release いずれの構成でもコンパイル・リンクでき、ImGui に依存しない
// 自作 UI（ECS 上のコンポーネント/システムとして実装。実体は TerminalLauncherUi）を
// すべての構成で描画する。ImGui によるランチャー UI（TerminalPanel）は比較対象・保険として
// Debug 構成のみ引き続き表示される。
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

    // --- v10: UI ウィンドウの切り離し / 再結合 ---

    /// 前フレームまでに立った UiWindow::detachRequested を処理する。
    /// scene_->Update() より前に呼ぶこと（このフレームの UiLayoutSystem が新しいサーフェス ID で
    /// 矩形を解決できるようにするため。1 フレーム遅れての反映になるが、閉じるボタンの
    /// closeRequested 同様、既存の「立てるのはシステム、拾って処理するのはアプリ」という
    /// 責務分担に沿っている）。
    void HandlePendingDetachRequests();
    /// 指定した UI ウィンドウを、現在の矩形の位置に新しい OS ウィンドウとして切り離す。
    void DetachWindow(const OriGine::EntityHandle& _root);
    /// nativeWindows_->BeginFrame() が破棄してしまう前に、切り離し中のウィンドウが
    /// 閉じられようとしていないかを見て、再結合に使うスクリーン矩形を控えておく。
    void CapturePendingReattachRects();
    /// このフレームで閉じられたサーフェスに対応する UI ウィンドウを、メインウィンドウへ戻す。
    void HandleClosedSurfaces();

    std::unique_ptr<OriGine::SceneManager>    sceneManager_ = nullptr;
    std::unique_ptr<LogGuide::SessionCatalog> catalog_      = nullptr;
    std::unique_ptr<OriGine::Scene>           scene_        = nullptr; // ECS 上の自作 UI (ウィンドウ) を描画するためのシーン
    std::string                               lastError_;

    // v13: 自作 UI によるランチャーの実 UI (レコーダー起動 / セッション一覧 / 再生)。
    // ImGui 版 (TerminalPanel, Debug 専用) と同じことを全構成で行う。
    LogGuide::TerminalLauncherUi               launcherUi_;

    // v14: ドックツリー/タブの確認用デモ (ドックスペース + ダミーウィンドウ 4 枚)。
    // ランチャー本体とは無関係の動作確認用で、v15 でドラッグ&ドロップのドッキングを
    // 実装する際にここへ差し替えていく想定。
    LogGuide::UiDockDemo                       dockDemo_;

    // v9: 追加の OS ウィンドウ基盤。v10 からは UI ウィンドウの切り離し/再結合が実際に使う
    // (v9 時点の F2/F3 確認用ショートカットは、切り離しが動くようになったので削除した)。
    std::unique_ptr<LogGuide::NativeWindowManager> nativeWindows_ = nullptr;

    // v10: 切り離し/再結合の対象になる UI ウィンドウのルート一覧 (v13 からはランチャーウィンドウ 1 枚)。
    std::vector<OriGine::EntityHandle> uiWindowRoots_;
    // v10: 切り離し中のウィンドウ。サーフェス ID → そのウィンドウのルート。
    std::unordered_map<int32_t, OriGine::EntityHandle> detachedWindowsBySurface_;

    /// 再結合に使う、閉じる直前の OS ウィンドウのスクリーン座標での矩形。
    struct PendingReattachRect {
        OriGine::Vec2f screenOrigin{}; // クライアント左上のスクリーン座標
        OriGine::Vec2f clientSize{};   // クライアント領域のサイズ
    };
    // v10: CapturePendingReattachRects() が控え、HandleClosedSurfaces() が消費する。
    std::unordered_map<int32_t, PendingReattachRect> pendingReattachRects_;
};
