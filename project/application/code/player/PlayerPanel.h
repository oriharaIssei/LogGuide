#pragma once

/// api
#include <windows.h>

// ImGui による 2 動画同期再生 UI。ImGui は _DEBUG ビルドでのみ有効。
namespace LogGuide {

class DualPlayerController;
class WindowFileDrop;

// 1 フレーム分の再生コントロールウィンドウを描画する。
// Engine::BeginFrame と EndFrame の間で呼ぶこと。owner はダイアログの親ウィンドウ。
void DrawPlayerPanel(DualPlayerController& player, WindowFileDrop& drop, HWND owner);

} // namespace LogGuide
