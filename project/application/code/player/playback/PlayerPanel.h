#pragma once

/// api
#include <windows.h>

// ImGui による 2 動画同期再生 UI。ImGui は _DEBUG ビルドでのみ有効。
namespace LogGuide {

class DualPlayerController;
class WindowFileDrop;
class SummaryService;

// 1 フレーム分の再生コントロールウィンドウを描画する。
// Engine::BeginFrame と EndFrame の間で呼ぶこと。owner はダイアログの親ウィンドウ。
// summary は「要約を生成」ボタン用（null 可＝ボタン非表示）。
void DrawPlayerPanel(DualPlayerController& player, WindowFileDrop& drop, HWND owner,
                     SummaryService* summary = nullptr);

} // namespace LogGuide
