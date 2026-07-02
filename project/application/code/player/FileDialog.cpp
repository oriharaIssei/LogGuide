#include "player/FileDialog.h"

/// api
#include <commdlg.h>
#include <shellapi.h>

/// stl
#include <unordered_map>

// GetOpenFileName / Drag* API のインポートライブラリ(MSVC 用に自己完結でリンク指定)。
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "shell32.lib")

namespace LogGuide {

namespace {

std::string WideToUtf8(const std::wstring& w) {
    if (w.empty()) {
        return {};
    }
    int needed = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    if (needed <= 0) {
        return {};
    }
    std::string out(static_cast<size_t>(needed), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), out.data(), needed, nullptr, nullptr);
    return out;
}

// サブクラス化したウィンドウごとの WindowFileDrop を引くための静的テーブル。
std::unordered_map<HWND, WindowFileDrop*>& DropRegistry() {
    static std::unordered_map<HWND, WindowFileDrop*> registry;
    return registry;
}

} // namespace

std::string OpenFileDialog(HWND owner, const wchar_t* filterUtf16) {
    wchar_t buffer[MAX_PATH] = {};

    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = owner;
    ofn.lpstrFilter = filterUtf16;
    ofn.lpstrFile   = buffer;
    ofn.nMaxFile    = MAX_PATH;
    ofn.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

    if (!GetOpenFileNameW(&ofn)) {
        return {};
    }
    return WideToUtf8(buffer);
}

void WindowFileDrop::Initialize(HWND hwnd) {
    if (!hwnd || hwnd_) {
        return;
    }
    hwnd_ = hwnd;
    DropRegistry()[hwnd_] = this;

    DragAcceptFiles(hwnd_, TRUE);
    originalProc_ = reinterpret_cast<WNDPROC>(
        SetWindowLongPtrW(hwnd_, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&WindowFileDrop::ThunkProc)));
}

void WindowFileDrop::Finalize() {
    if (!hwnd_) {
        return;
    }
    if (originalProc_) {
        SetWindowLongPtrW(hwnd_, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(originalProc_));
        originalProc_ = nullptr;
    }
    DragAcceptFiles(hwnd_, FALSE);
    DropRegistry().erase(hwnd_);
    hwnd_ = nullptr;
}

void WindowFileDrop::PushDropped(std::vector<std::string>&& paths) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& p : paths) {
        dropped_.push_back(std::move(p));
    }
}

std::vector<std::string> WindowFileDrop::PollDropped() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> out = std::move(dropped_);
    dropped_.clear();
    return out;
}

LRESULT CALLBACK WindowFileDrop::ThunkProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    auto& registry = DropRegistry();
    auto  it       = registry.find(hwnd);
    WindowFileDrop* self = (it != registry.end()) ? it->second : nullptr;
    WNDPROC original     = self ? self->originalProc_ : nullptr;

    if (msg == WM_DROPFILES && self) {
        HDROP hDrop  = reinterpret_cast<HDROP>(wparam);
        UINT  count  = DragQueryFileW(hDrop, 0xFFFFFFFF, nullptr, 0);
        std::vector<std::string> paths;
        paths.reserve(count);
        for (UINT i = 0; i < count; ++i) {
            UINT len = DragQueryFileW(hDrop, i, nullptr, 0);
            std::wstring path(len, L'\0');
            DragQueryFileW(hDrop, i, path.data(), len + 1);
            paths.push_back(WideToUtf8(path));
        }
        DragFinish(hDrop);
        self->PushDropped(std::move(paths));
        return 0;
    }

    if (original) {
        return CallWindowProcW(original, hwnd, msg, wparam, lparam);
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

} // namespace LogGuide
