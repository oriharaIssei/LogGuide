#include "recording/RecordingSystem.h"

/// module
#include "recording/SessionManifest.h"

/// engine
#define ENGINE_INCLUDE
#define ENGINE_MEDIA_CAPTURE
#include <EngineInclude.h>

#include "Engine.h"
#include "logger/Logger.h"

/// windows (UTF-8 変換用)
#include <windows.h>

/// stl
#include <cstdio>
#include <ctime>
#include <filesystem>

namespace fs = std::filesystem;

namespace LogGuide {

namespace {

// std::wstring -> UTF-8。
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

// "session_YYYYMMDD_HHMMSS" 形式のセッション ID を作る。
std::string MakeSessionId(const std::tm& lt) {
    char buf[32] = {};
    std::snprintf(buf, sizeof(buf), "session_%04d%02d%02d_%02d%02d%02d",
        lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday, lt.tm_hour, lt.tm_min, lt.tm_sec);
    return buf;
}

// ISO8601（ローカル時刻, 秒精度）。
std::string ToIso8601(const std::tm& lt) {
    char buf[32] = {};
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d",
        lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday, lt.tm_hour, lt.tm_min, lt.tm_sec);
    return buf;
}

std::tm LocalNow() {
    std::time_t t = std::time(nullptr);
    std::tm lt{};
    localtime_s(&lt, &t);
    return lt;
}

} // namespace

RecordingSystem::RecordingSystem()  = default;
RecordingSystem::~RecordingSystem() {
    Finalize();
}

void RecordingSystem::Initialize(OriGine::Engine* engine) {
    engine_ = engine;

    // Mp4Recorder / WebCamera / Mp4Player は Media Foundation を要求する。
    OriGine::WebCamera::StaticInitialize();
    mfInitialized_ = true;

    camera_         = std::make_unique<OriGine::WebCamera>();
    microphone_     = std::make_unique<OriGine::Microphone>();
    screen_         = std::make_unique<OriGine::ScreenCapture>();
    systemAudio_    = std::make_unique<OriGine::SystemAudioCapture>();
    cameraRecorder_ = std::make_unique<OriGine::MediaRecorder>();
    screenRecorder_ = std::make_unique<OriGine::ScreenRecorder>();

    RefreshDevices();
}

void RecordingSystem::Finalize() {
    if (state_ == State::Recording) {
        StopRecording();
    }
    cameraRecorder_.reset();
    screenRecorder_.reset();
    camera_.reset();
    microphone_.reset();
    screen_.reset();
    systemAudio_.reset();

    if (mfInitialized_) {
        OriGine::WebCamera::StaticFinalize();
        mfInitialized_ = false;
    }
    engine_ = nullptr;
}

void RecordingSystem::RefreshDevices() {
    cameras_.clear();
    microphones_.clear();
    monitorNames_.clear();

    for (const auto& d : OriGine::WebCamera::EnumerateDevices()) {
        cameras_.push_back({WideToUtf8(d.name), d.id});
    }
    for (const auto& d : OriGine::Microphone::EnumerateDevices()) {
        microphones_.push_back({WideToUtf8(d.name), d.id});
    }
    for (const auto& m : OriGine::ScreenCapture::EnumerateMonitors()) {
        std::string name = WideToUtf8(m.name);
        name += " (" + std::to_string(m.width) + "x" + std::to_string(m.height) + ")";
        monitorNames_.push_back(std::move(name));
    }
}

double RecordingSystem::GetElapsedSeconds() const {
    if (state_ != State::Recording) {
        return session_.durationSeconds;
    }
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration<double>(now - startClock_).count();
}

bool RecordingSystem::StartCameraTrack(const SessionInfo& session, std::vector<TrackInfo>& outTracks) {
    const std::wstring camId =
        (settings_.cameraIndex >= 0 && settings_.cameraIndex < static_cast<int>(cameras_.size()))
            ? cameras_[settings_.cameraIndex].id
            : std::wstring{};

    if (!camera_->Open(camId, settings_.cameraWidth, settings_.cameraHeight)) {
        lastError_ = "failed to open web camera";
        return false;
    }
    camera_->StartCapture();

    OriGine::Microphone* micPtr = nullptr;
    std::string micName;
    if (settings_.recordMic) {
        const std::wstring micId =
            (settings_.micIndex >= 0 && settings_.micIndex < static_cast<int>(microphones_.size()))
                ? microphones_[settings_.micIndex].id
                : std::wstring{};
        if (!microphone_->Open(micId)) {
            lastError_ = "failed to open microphone";
            return false;
        }
        microphone_->StartCapture();
        micPtr = microphone_.get();
        micName = (settings_.micIndex >= 0 && settings_.micIndex < static_cast<int>(microphones_.size()))
                      ? microphones_[settings_.micIndex].name
                      : "default";
    }

    OriGine::MediaRecorder::Config cfg;
    cfg.fps         = settings_.fps;
    cfg.recordAudio = settings_.recordMic;

    const std::string path = (fs::path(session.directory) / "camera.mp4").string();
    if (!cameraRecorder_->Start(camera_.get(), micPtr, path, cfg)) {
        lastError_ = "MediaRecorder: " + cameraRecorder_->GetLastError();
        return false;
    }

    TrackInfo t;
    t.kind            = TrackKind::CameraMic;
    t.label           = "camera";
    t.file            = "camera.mp4";
    t.absolutePath    = path;
    t.videoDeviceName = (settings_.cameraIndex >= 0 && settings_.cameraIndex < static_cast<int>(cameras_.size()))
                            ? cameras_[settings_.cameraIndex].name
                            : "default";
    t.width         = camera_->GetWidth();
    t.height        = camera_->GetHeight();
    t.fps           = cfg.fps;
    t.videoBitrate  = cfg.videoBitrate;
    t.hasAudio      = settings_.recordMic;
    t.audioDeviceName = micName;
    t.audioBitrate  = settings_.recordMic ? cfg.audioBitrate : 0;
    outTracks.push_back(std::move(t));
    return true;
}

bool RecordingSystem::StartScreenTrack(const SessionInfo& session, std::vector<TrackInfo>& outTracks) {
    if (!engine_) {
        lastError_ = "engine is null";
        return false;
    }

    if (!screen_->Open(engine_->GetDxDevice(), engine_->GetDxCommand(), static_cast<uint32_t>(settings_.monitorIndex))) {
        lastError_ = "ScreenCapture: " + screen_->GetLastError();
        return false;
    }
    screen_->StartCapture();

    OriGine::SystemAudioCapture* sysPtr = nullptr;
    if (settings_.recordSystemAudio) {
        if (!systemAudio_->Open()) {
            lastError_ = "failed to open system audio (eRender loopback)";
            return false;
        }
        systemAudio_->StartCapture();
        sysPtr = systemAudio_.get();
    }

    OriGine::ScreenRecorder::Config cfg;
    cfg.fps         = settings_.fps;
    cfg.recordAudio = settings_.recordSystemAudio;

    const std::string path = (fs::path(session.directory) / "screen.mp4").string();
    if (!screenRecorder_->Start(screen_.get(), sysPtr, path, cfg)) {
        lastError_ = "ScreenRecorder: " + screenRecorder_->GetLastError();
        return false;
    }

    std::string monitorName = "monitor " + std::to_string(settings_.monitorIndex);
    if (settings_.monitorIndex >= 0 && settings_.monitorIndex < static_cast<int>(monitorNames_.size())) {
        monitorName = monitorNames_[settings_.monitorIndex];
    }

    TrackInfo t;
    t.kind            = TrackKind::ScreenSystemAudio;
    t.label           = "screen";
    t.file            = "screen.mp4";
    t.absolutePath    = path;
    t.videoDeviceName = monitorName;
    t.width           = screen_->GetWidth();
    t.height          = screen_->GetHeight();
    t.fps             = cfg.fps;
    t.videoBitrate    = cfg.videoBitrate;
    t.hasAudio        = settings_.recordSystemAudio;
    t.audioDeviceName = settings_.recordSystemAudio ? "system audio (eRender loopback)" : "";
    t.audioBitrate    = settings_.recordSystemAudio ? cfg.audioBitrate : 0;
    outTracks.push_back(std::move(t));
    return true;
}

bool RecordingSystem::StartRecording() {
    if (state_ != State::Idle) {
        return false;
    }
    if (!settings_.recordCameraTrack && !settings_.recordScreenTrack) {
        lastError_ = "no track selected";
        return false;
    }
    lastError_.clear();

    const std::tm lt = LocalNow();

    SessionInfo session;
    session.sessionId    = MakeSessionId(lt);
    session.startedAtIso = ToIso8601(lt);
    session.directory    = (fs::path(settings_.outputRoot) / session.sessionId).string();

    std::error_code ec;
    fs::create_directories(session.directory, ec);
    if (ec) {
        lastError_ = "failed to create session directory: " + ec.message();
        return false;
    }

    std::vector<TrackInfo> tracks;
    if (settings_.recordCameraTrack) {
        if (!StartCameraTrack(session, tracks)) {
            TeardownCaptures();
            return false;
        }
    }
    if (settings_.recordScreenTrack) {
        if (!StartScreenTrack(session, tracks)) {
            TeardownCaptures();
            return false;
        }
    }

    session.tracks = std::move(tracks);
    session_       = std::move(session);
    startClock_    = std::chrono::steady_clock::now();
    state_         = State::Recording;

    LOG_INFO("RecordingSystem: started session '{}' ({} track(s))", session_.sessionId, session_.tracks.size());
    return true;
}

void RecordingSystem::TeardownCaptures() {
    // レコーダを先に止めてキャプチャコールバックを外してから、キャプチャを閉じる。
    cameraRecorder_->Stop();
    screenRecorder_->Stop();

    camera_->StopCapture();
    camera_->Close();
    microphone_->StopCapture();
    microphone_->Close();
    screen_->StopCapture();
    screen_->Close();
    systemAudio_->StopCapture();
    systemAudio_->Close();
}

void RecordingSystem::StopRecording() {
    if (state_ != State::Recording) {
        return;
    }

    const double elapsed = GetElapsedSeconds();

    TeardownCaptures();

    const std::tm lt         = LocalNow();
    session_.endedAtIso      = ToIso8601(lt);
    session_.durationSeconds = elapsed;

    // 2 つの mp4 とその情報を統合するマニフェスト(session.json)を書き出す。
    const std::string manifestPath = (fs::path(session_.directory) / "session.json").string();
    std::string err;
    if (!WriteSessionManifest(session_, manifestPath, &err)) {
        lastError_ = err;
        LOG_ERROR("RecordingSystem: {}", err);
    } else {
        LOG_INFO("RecordingSystem: saved session manifest '{}'", manifestPath);
    }

    lastSession_ = session_;
    state_       = State::Idle;
}

} // namespace LogGuide
