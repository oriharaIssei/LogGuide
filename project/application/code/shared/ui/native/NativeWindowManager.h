#pragma once

#include "ui/native/NativeWindow.h"

/// stl
#include <cstdint>
#include <memory>
#include <vector>

/// math
#include <Vector2.h>

namespace LogGuide {

/// 追加の OS ウィンドウ (NativeWindow) をまとめて管理するクラス.
/// 「サーフェス ID」(int32_t) でウィンドウを引く。0 はメインウィンドウ (engine の WinApp) の
/// 予約 ID で、このクラスが保持するウィンドウには含まれない。追加ウィンドウの ID は 1 から
/// 単調増加で払い出し、使い回さない。
///
/// 所有者はアプリ (TerminalApp)。シングルトンにはしない。v10 で ECS のシステムから
/// 参照できるよう、生ポインタとして持ち回せる設計にしてある (システム側に setter を足すだけでよい)。
class NativeWindowManager {
public:
    NativeWindowManager()  = default;
    ~NativeWindowManager() = default;

    /// ウィンドウを生成し、サーフェス ID を返す。失敗したら -1.
    int32_t Open(const NativeWindow::Desc& _desc);
    /// 指定サーフェスに閉じる要求を出す (WM_CLOSE を送るのと同じ経路)。
    /// 実際の破棄は次の BeginFrame() で行われる。
    void RequestClose(int32_t _surfaceId);
    /// 指定サーフェス ID のウィンドウを取得する。見つからなければ nullptr.
    NativeWindow* Get(int32_t _surfaceId);

    /// 全ウィンドウの BeginFrame() を呼ぶ。閉じる要求が出ているウィンドウはここで実際に破棄する。
    void BeginFrame();
    /// 全ウィンドウを単色で BindAndClear() → EndRender() するだけの確認用 (v9 時点のもの)。
    /// v10 では実際の描画は UiRenderSystem がサーフェスごとに BindAndClear()/EndRender() を
    /// 直接呼んで行うため、通常のフレーム経路からは呼ばない (残しているのは単色クリアだけで
    /// 動作確認したいとき用)。
    void RenderAll();
    /// 全ウィンドウの Present() を呼ぶ。Engine::ScreenPostDraw() の後に呼ぶこと。
    void PresentAll();
    /// 全ウィンドウを破棄する (アプリ終了時に呼ぶ)。
    void CloseAll();

    size_t Count() const { return windows_.size(); }

    /// このフレームで閉じられた (破棄された) サーフェス ID の一覧。
    /// 次に BeginFrame() を呼ぶとクリアされる。アプリが後始末 (自作 UI 側の紐付け解除等) に使う。
    const std::vector<int32_t>& TakeClosedSurfaces() const { return closedSurfaces_; }

    // ===== サーフェス問い合わせ (v10 で UI システムが使う。ここで作っておく) =====

    /// _surfaceId == 0 ならメインウィンドウ (engine の WinApp) のサイズを答える。
    OriGine::Vec2f GetSurfaceSize(int32_t _surfaceId) const;
    /// クライアント座標でのカーソル位置。
    OriGine::Vec2f GetSurfaceCursorPos(int32_t _surfaceId) const;
    /// カーソルが指定サーフェスの上にあるか (他のウィンドウに隠れていない)。
    bool IsSurfaceUnderCursor(int32_t _surfaceId) const;
    /// 指定サーフェス ID が現在有効か (0 は常に true)。
    bool IsSurfaceValid(int32_t _surfaceId) const;
    /// 指定サーフェスのクライアント左上のスクリーン座標を返す (_surfaceId == 0 ならメインウィンドウ)。
    /// v10: 切り離し/再結合で UI ウィンドウの矩形とスクリーン座標を相互変換するために使う。
    OriGine::Vec2f GetSurfaceScreenOrigin(int32_t _surfaceId) const;
    /// 現在開いている追加ウィンドウのサーフェス ID 一覧 (昇順。メインウィンドウの 0 は含まない)。
    /// v10: UiRenderSystem が「UI が乗っていないサーフェスも含めて全部クリアする」ために使う。
    std::vector<int32_t> GetSurfaceIds() const;

    // ===== UI 用のマウスボタン状態 (v11) =====
    // engine の MouseInput は DirectInput を DISCL_FOREGROUND でメインウィンドウに結び付けている
    // ため、切り離した OS ウィンドウにフォーカスがあるとデバイスが Unacquire されボタンが
    // 取れなくなる (カーソル位置は GetCursorPos なので効くが、クリックだけ死ぬ)。UI はカーソル
    // 位置を既に自前で取っているので、ボタンも自前で取る。

    /// UI 用のマウスボタン状態を 1 フレーム分更新する.
    /// TerminalApp::Frame() の scene_->Update() より前に 1 回だけ呼ぶこと.
    void UpdateMouseState();

    bool IsMouseDown() const    { return mouseDown_; }
    bool IsMouseTrigger() const { return mouseDown_ && !mouseDownPrev_; }
    bool IsMouseRelease() const { return !mouseDown_ && mouseDownPrev_; }

    /// このアプリが所有するウィンドウ (メイン or 追加ウィンドウ) のいずれかが前面か.
    bool IsAppForeground() const;

private:
    /// ウィンドウ 1 枚とそのサーフェス ID の組.
    struct Entry {
        int32_t id = 0;
        std::unique_ptr<NativeWindow> window;
    };

    /// サーフェス ID からウィンドウを探す (const 版)。GetSurfaceXxx() 系から使う。
    const NativeWindow* Find(int32_t _surfaceId) const;

    std::vector<Entry> windows_;
    int32_t nextSurfaceId_ = 1; // 0 はメインウィンドウ予約
    std::vector<int32_t> closedSurfaces_;

    bool mouseDown_     = false;
    bool mouseDownPrev_ = false;
};

} // namespace LogGuide
