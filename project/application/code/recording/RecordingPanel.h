#pragma once

// ImGui による録画コントロール UI。
// ImGui はこのエンジンでは _DEBUG ビルドでのみ有効なため、実装全体を _DEBUG で囲む。
// 非 _DEBUG ビルドでは空関数となり、録画ロジック(RecordingSystem)自体はビルド構成に依存しない。

namespace LogGuide {

class RecordingSystem;

// 1 フレーム分の録画コントロールウィンドウを描画する。
// Engine::BeginFrame と EndFrame の間（ImGui フレーム内）で呼ぶこと。
void DrawRecordingPanel(RecordingSystem& system);

} // namespace LogGuide
