#pragma once

/// stl
#include <chrono>
#include <memory>
#include <string>
#include <vector>

/// module
#include "recording/RecordingComponents.h"

namespace OriGine {
class Engine;
class WebCamera;
class Microphone;
class ScreenCapture;
class SystemAudioCapture;
class MediaRecorder;
class ScreenRecorder;
} // namespace OriGine

namespace LogGuide {

// =============================================================================
// RecordingSystem
//
// 録画モジュールの「ロジック」層。エンジンのキャプチャ/レコーダ群を束ね、
//   camera.mp4 = WebCamera + Microphone   (MediaRecorder)
//   screen.mp4 = ScreenCapture + SystemAudioCapture(eRender) (ScreenRecorder)
// を同時に録り、停止時に 2 つの mp4 と session.json を 1 セッションディレクトリへ統合する。
//
// 現状は素の C++ クラスだが、状態を RecordingSettings / SessionInfo という
// POD（=将来の Component）に閉じ込め、振る舞いをこのクラス（=将来の ISystem）に
// 集約することで ECS 移行を容易にしている。
//
// キャプチャ各クラスは内部スレッドで動くため、Start 後にアプリ側が毎フレーム
// ポンプする必要はない。UI 更新のためのヘルパ（経過秒など）のみ提供する。
// =============================================================================
class RecordingSystem {
public:
    enum class State {
        Idle,
        Recording,
    };

    // UI 表示用のデバイス一覧（UTF-8 名 + 内部 ID を分離して保持）。
    struct DeviceEntry {
        std::string  name; // 表示名 (UTF-8)
        std::wstring id;   // エンジンに渡す ID
    };

    RecordingSystem();
    ~RecordingSystem();

    RecordingSystem(const RecordingSystem&)            = delete;
    RecordingSystem& operator=(const RecordingSystem&) = delete;

    // Media Foundation の起動とデバイス列挙を行う。engine は録画中も生存している必要がある。
    void Initialize(OriGine::Engine* engine);
    // 録画中なら停止し、Media Foundation を終了する。
    void Finalize();

    // デバイス一覧を再列挙する（UI の「更新」ボタン用）。
    void RefreshDevices();

    // 現在の settings_ に従って録画を開始する。失敗時 false（GetLastError で理由取得）。
    bool StartRecording();
    // 録画を停止し、session.json を書き出してセッションを確定する。
    void StopRecording();

    State              GetState() const { return state_; }
    bool               IsRecording() const { return state_ == State::Recording; }
    double             GetElapsedSeconds() const;
    const std::string& GetLastError() const { return lastError_; }

    RecordingSettings&       Settings() { return settings_; }
    const RecordingSettings& Settings() const { return settings_; }

    const std::vector<DeviceEntry>&    Cameras() const { return cameras_; }
    const std::vector<DeviceEntry>&    Microphones() const { return microphones_; }
    const std::vector<std::string>&    Monitors() const { return monitorNames_; }

    // 直近に完了したセッション情報（UI で保存先を案内するために保持）。
    const SessionInfo& LastSession() const { return lastSession_; }
    bool               HasLastSession() const { return !lastSession_.sessionId.empty(); }

private:
    // 失敗時に開いたデバイス/レコーダをすべて閉じる（session.json は書かない）。
    void TeardownCaptures();
    // camera.mp4 の録画を開始し、track を outTracks に追加する。
    bool StartCameraTrack(const SessionInfo& session, std::vector<TrackInfo>& outTracks);
    // screen.mp4 の録画を開始し、track を outTracks に追加する。
    bool StartScreenTrack(const SessionInfo& session, std::vector<TrackInfo>& outTracks);

    OriGine::Engine* engine_ = nullptr;

    std::unique_ptr<OriGine::WebCamera>          camera_;
    std::unique_ptr<OriGine::Microphone>         microphone_;
    std::unique_ptr<OriGine::ScreenCapture>      screen_;
    std::unique_ptr<OriGine::SystemAudioCapture> systemAudio_;
    std::unique_ptr<OriGine::MediaRecorder>      cameraRecorder_;
    std::unique_ptr<OriGine::ScreenRecorder>     screenRecorder_;

    RecordingSettings settings_;
    SessionInfo       session_;     // 録画中の作業用
    SessionInfo       lastSession_; // 直近に確定したセッション

    std::vector<DeviceEntry> cameras_;
    std::vector<DeviceEntry> microphones_;
    std::vector<std::string> monitorNames_;

    State                                 state_ = State::Idle;
    std::chrono::steady_clock::time_point  startClock_{};
    bool                                  mfInitialized_ = false;
    std::string                           lastError_;
};

} // namespace LogGuide
