#pragma once

/// api
#include <windows.h>

/// stl
#include <mutex>
#include <string>
#include <vector>

namespace LogGuide {

// 「ファイルを開く」ダイアログ(単一選択)を表示し、選択パスを UTF-8 で返す。
// キャンセル時は空文字列。filterUtf16 は GetOpenFileName 形式("表示\0*.mp4\0..\0\0")。
std::string OpenFileDialog(HWND owner, const wchar_t* filterUtf16);

// =============================================================================
// WindowFileDrop
//
// 既存ウィンドウをサブクラス化して WM_DROPFILES を捕捉し、ドロップされたファイル
// パスを蓄積する。元の WndProc(エンジンの WinApp::WindowProc → ImGui 転送)は
// そのまま連鎖させるので、ImGui 入力などには影響しない。
// =============================================================================
class WindowFileDrop {
public:
    WindowFileDrop()  = default;
    ~WindowFileDrop() { Finalize(); }

    void Initialize(HWND hwnd);
    void Finalize();

    // 蓄積済みのドロップパス(UTF-8)を取り出してクリアする。
    std::vector<std::string> PollDropped();

private:
    static LRESULT CALLBACK ThunkProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
    void PushDropped(std::vector<std::string>&& paths);

    HWND     hwnd_         = nullptr;
    WNDPROC  originalProc_ = nullptr;

    std::mutex               mutex_;
    std::vector<std::string> dropped_;
};

} // namespace LogGuide
