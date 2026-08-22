#pragma once

/// stl
#include <string>

/// module
#include "recording/RecordingComponents.h"

// =============================================================================
// SessionManifest
//
// 「2 つの mp4 とその情報を統合したファイル」= session.json を読み書きする。
// mp4 本体（camera.mp4 / screen.mp4）はセッションディレクトリに置かれ、
// この JSON がそれらへの参照と録画メタデータ（デバイス名・解像度・fps・
// ビットレート・録画時刻・尺）を保持する統合マニフェストとして機能する。
// =============================================================================

namespace LogGuide {

// SessionInfo を jsonPath に JSON として書き出す。失敗時は false を返し err に理由を格納。
bool WriteSessionManifest(const SessionInfo& session, const std::string& jsonPath, std::string* err = nullptr);

// jsonPath から SessionInfo を読み込む。失敗時は false。
bool ReadSessionManifest(const std::string& jsonPath, SessionInfo& outSession, std::string* err = nullptr);

} // namespace LogGuide
