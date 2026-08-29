#include "ui/native/NativeWindowManager.h"

/// engine
#include "Engine.h"
#include "input/InputManager.h" // v12: メインウィンドウのホイール回転量を取るために要る
#include "input/MouseInput.h"
#include "winApp/WinApp.h"

using namespace OriGine;

namespace LogGuide {

namespace {
/// サブウィンドウごとに違う色にして、個別に描けていることを目で確認するための適当な色。
/// (ちゃんとした色相計算をする必要はないので、固定パレットを ID で回すだけにする)
constexpr OriGine::Vec4f kDebugClearColors[] = {
    {0.16f, 0.28f, 0.42f, 1.0f},
    {0.42f, 0.20f, 0.24f, 1.0f},
    {0.20f, 0.40f, 0.26f, 1.0f},
    {0.40f, 0.36f, 0.16f, 1.0f},
    {0.32f, 0.20f, 0.42f, 1.0f},
    {0.16f, 0.38f, 0.40f, 1.0f},
};
constexpr size_t kDebugClearColorCount = sizeof(kDebugClearColors) / sizeof(kDebugClearColors[0]);
} // namespace

int32_t NativeWindowManager::Open(const NativeWindow::Desc& _desc) {
    auto window = std::make_unique<NativeWindow>();
    if (!window->Create(_desc)) {
        return -1;
    }

    const int32_t id = nextSurfaceId_++;
    window->SetClearColor(kDebugClearColors[static_cast<size_t>(id) % kDebugClearColorCount]);

    windows_.push_back(Entry{id, std::move(window)});
    return id;
}

void NativeWindowManager::RequestClose(int32_t _surfaceId) {
    NativeWindow* window = Get(_surfaceId);
    if (window == nullptr) {
        return;
    }
    // closeRequested_ を直接書き換える setter は用意していない。閉じるボタンを押したのと
    // 同じ経路 (WM_CLOSE) で処理させる。SendMessage は WndProc を同期的に呼ぶので、この関数から
    // 戻った時点で IsCloseRequested() は true になっている。
    SendMessage(window->GetHwnd(), WM_CLOSE, 0, 0);
}

NativeWindow* NativeWindowManager::Get(int32_t _surfaceId) {
    return const_cast<NativeWindow*>(Find(_surfaceId));
}

const NativeWindow* NativeWindowManager::Find(int32_t _surfaceId) const {
    for (const auto& entry : windows_) {
        if (entry.id == _surfaceId) {
            return entry.window.get();
        }
    }
    return nullptr;
}

void NativeWindowManager::BeginFrame() {
    closedSurfaces_.clear();

    for (auto it = windows_.begin(); it != windows_.end();) {
        if (it->window->IsCloseRequested()) {
            it->window->Destroy();
            closedSurfaces_.push_back(it->id);
            it = windows_.erase(it);
        } else {
            it->window->BeginFrame();
            ++it;
        }
    }
}

void NativeWindowManager::RenderAll() {
    for (auto& entry : windows_) {
        entry.window->BindAndClear();
        entry.window->EndRender();
    }
}

void NativeWindowManager::PresentAll() {
    for (auto& entry : windows_) {
        entry.window->Present();
    }
}

void NativeWindowManager::CloseAll() {
    for (auto& entry : windows_) {
        entry.window->Destroy();
    }
    windows_.clear();
}

OriGine::Vec2f NativeWindowManager::GetSurfaceSize(int32_t _surfaceId) const {
    if (_surfaceId == 0) {
        return Engine::GetInstance()->GetWinApp()->GetWindowSize();
    }
    const NativeWindow* window = Find(_surfaceId);
    if (window == nullptr) {
        return OriGine::Vec2f(0.0f, 0.0f);
    }
    return window->GetClientSize();
}

OriGine::Vec2f NativeWindowManager::GetSurfaceCursorPos(int32_t _surfaceId) const {
    if (_surfaceId == 0) {
        POINT pt{};
        GetCursorPos(&pt);
        ScreenToClient(Engine::GetInstance()->GetWinApp()->GetHwnd(), &pt);
        return OriGine::Vec2f(static_cast<float>(pt.x), static_cast<float>(pt.y));
    }
    const NativeWindow* window = Find(_surfaceId);
    if (window == nullptr) {
        return OriGine::Vec2f(0.0f, 0.0f);
    }
    POINT pt{};
    GetCursorPos(&pt);
    return window->ScreenToClientPos(pt);
}

bool NativeWindowManager::IsSurfaceUnderCursor(int32_t _surfaceId) const {
    if (_surfaceId == 0) {
        POINT pt{};
        GetCursorPos(&pt);
        HWND hwnd = Engine::GetInstance()->GetWinApp()->GetHwnd();
        HWND hit  = WindowFromPoint(pt);
        return hit == hwnd || IsChild(hwnd, hit) != FALSE;
    }
    const NativeWindow* window = Find(_surfaceId);
    return window != nullptr && window->IsCursorOver();
}

bool NativeWindowManager::IsSurfaceValid(int32_t _surfaceId) const {
    if (_surfaceId == 0) {
        return true; // メインウィンドウは常に有効
    }
    return Find(_surfaceId) != nullptr;
}

OriGine::Vec2f NativeWindowManager::GetSurfaceScreenOrigin(int32_t _surfaceId) const {
    if (_surfaceId == 0) {
        POINT origin{0, 0};
        ClientToScreen(Engine::GetInstance()->GetWinApp()->GetHwnd(), &origin);
        return OriGine::Vec2f(static_cast<float>(origin.x), static_cast<float>(origin.y));
    }
    const NativeWindow* window = Find(_surfaceId);
    if (window == nullptr) {
        return OriGine::Vec2f(0.0f, 0.0f);
    }
    return window->ClientToScreenPos(POINT{0, 0});
}

void NativeWindowManager::UpdateMouseState() {
    mouseDownPrev_ = mouseDown_;

    // v12: 各追加ウィンドウが WM_MOUSEWHEEL でためたホイール回転量を控えてから 0 に戻す。
    // ここで一旦控えておかないと、GetSurfaceWheelDelta() を呼ぶタイミング (UiScrollSystem は
    // StateTransition カテゴリ) によっては scene_->Update() の途中で消えてしまう。
    surfaceWheelDelta_.clear();
    for (auto& entry : windows_) {
        surfaceWheelDelta_[entry.id] = entry.window->GetWheelDelta();
        entry.window->ResetWheelDelta();
    }

    // 他のアプリを操作している間は UI が反応しないようにする。
    // GetAsyncKeyState はシステム全体の物理状態なので、この判定が無いと
    // 他アプリでのクリックまで拾ってしまう。
    if (!IsAppForeground()) {
        mouseDown_ = false;
        return;
    }

    // 左右ボタンの入れ替え設定 (コントロールパネル) を考慮する。
    const int vkPrimary = GetSystemMetrics(SM_SWAPBUTTON) ? VK_RBUTTON : VK_LBUTTON;
    mouseDown_ = (GetAsyncKeyState(vkPrimary) & 0x8000) != 0;
}

int32_t NativeWindowManager::GetSurfaceWheelDelta(int32_t _surfaceId) const {
    if (_surfaceId == 0) {
        // メインウィンドウは従来通り engine (DirectInput) 経由。生値は WHEEL_DELTA (120) 単位なので、
        // 追加ウィンドウ側 (NativeWindow::wheelDelta_、WM_MOUSEWHEEL の時点で既にノッチ単位に正規化
        // 済み) と単位を揃えるためにここで割る。
        return OriGine::InputManager::GetInstance()->GetMouse()->GetWheelDelta() / WHEEL_DELTA;
    }
    auto it = surfaceWheelDelta_.find(_surfaceId);
    return it != surfaceWheelDelta_.end() ? it->second : 0;
}

bool NativeWindowManager::IsAppForeground() const {
    const HWND foreground = GetForegroundWindow();
    if (foreground == nullptr) {
        return false;
    }
    // メインウィンドウ
    if (Engine::GetInstance()->GetWinApp()->GetHwnd() == foreground) {
        return true;
    }
    // 追加ウィンドウ
    for (const auto& entry : windows_) {
        if (entry.window && entry.window->GetHwnd() == foreground) {
            return true;
        }
    }
    return false;
}

std::vector<int32_t> NativeWindowManager::GetSurfaceIds() const {
    // nextSurfaceId_ は単調増加で払い出し、windows_ からの削除は erase のみなので、
    // 挿入順 (= 昇順) がそのまま保たれている。
    std::vector<int32_t> ids;
    ids.reserve(windows_.size());
    for (const auto& entry : windows_) {
        ids.push_back(entry.id);
    }
    return ids;
}

} // namespace LogGuide
