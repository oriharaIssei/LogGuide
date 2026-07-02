#pragma once

/// stl
#include <cstdint>
#include <string>
#include <vector>

// =============================================================================
// 録画モジュールの「データ」層。
//
// ここに定義する構造体は純粋な POD（エンジン依存なし）であり、
// 最終的な ECS 移行時にそのまま Component へ昇格させることを想定している:
//   RecordingSettings -> RecordingConfigComponent（エンティティに録画設定を持たせる）
//   TrackInfo         -> CaptureTrackComponent   （1 mp4 = 1 トラックを表す）
//   SessionInfo       -> RecordingSessionComponent（録画セッション全体）
//
// ロジック（デバイスを開く・録画する・メタデータを書く）は RecordingSystem 側に置き、
// データとロジックを分離しておくことで System へ移行しやすくしている。
// =============================================================================

namespace LogGuide {

// トラック（=1 つの mp4）の種別。
// 将来 ECS 化する際は、この enum ごとに専用 System を割り当てても良い。
enum class TrackKind {
    CameraMic,         // WebCamera(映像) + Microphone(マイク音声)
    ScreenSystemAudio, // 指定スクリーン(映像) + SystemAudioCapture(eRender ループバック音声)
};

inline const char* ToString(TrackKind kind) {
    switch (kind) {
    case TrackKind::CameraMic:
        return "cameraMic";
    case TrackKind::ScreenSystemAudio:
        return "screenSystemAudio";
    }
    return "unknown";
}

// 録画された 1 トラック(=1 mp4)の情報。session.json にそのまま書き出される。
struct TrackInfo {
    TrackKind   kind = TrackKind::CameraMic;
    std::string label;           // "camera" / "screen"
    std::string file;            // セッションディレクトリからの相対パス ("camera.mp4")
    std::string absolutePath;    // 実書き出し先の絶対パス

    // 映像
    std::string videoDeviceName; // 映像デバイス名 (UTF-8)。screen の場合はモニタ名。
    uint32_t    width         = 0;
    uint32_t    height        = 0;
    uint32_t    fps           = 30;
    uint32_t    videoBitrate  = 0; // bps

    // 音声
    bool        hasAudio       = true;
    std::string audioDeviceName; // 音声デバイス名 (UTF-8)
    uint32_t    audioBitrate   = 0; // bps
};

// 録画セッション全体。camera.mp4 / screen.mp4 と session.json を束ねる論理単位。
struct SessionInfo {
    std::string sessionId;      // "session_YYYYMMDD_HHMMSS"
    std::string directory;      // セッション出力ディレクトリ(絶対パス)
    std::string startedAtIso;   // 録画開始時刻 (ISO8601, ローカル)
    std::string endedAtIso;     // 録画終了時刻 (ISO8601, ローカル)
    double      durationSeconds = 0.0;

    std::vector<TrackInfo> tracks;
};

// ユーザーが UI から編集する録画設定。ECS 移行時は RecordingConfigComponent 相当。
struct RecordingSettings {
    bool recordCameraTrack = true; // camera.mp4 を録るか
    bool recordScreenTrack = true; // screen.mp4 を録るか

    // -1 は「デフォルトデバイス」を意味する。
    int cameraIndex  = -1;
    int micIndex     = -1;
    int monitorIndex = 0;

    uint32_t cameraWidth  = 1280;
    uint32_t cameraHeight = 720;
    uint32_t fps          = 30;

    bool recordMic         = true; // camera トラックにマイク音声を含める
    bool recordSystemAudio = true; // screen トラックに eRender(システム音声)を含める

    std::string outputRoot = "recordings"; // セッションを作成する親ディレクトリ
};

} // namespace LogGuide
