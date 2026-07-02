#include "player/DualPlayerController.h"

/// module
#include "recording/SessionManifest.h"

/// engine
#define ENGINE_INCLUDE
#define ENGINE_MEDIA_CAPTURE
#include <EngineInclude.h>

#include "logger/Logger.h"

/// stl
#include <algorithm>
#include <filesystem>

namespace fs = std::filesystem;

namespace LogGuide {

namespace {
// マスターと追従側のズレがこの秒数を超えたら Seek で引き戻す。
constexpr double kSyncThresholdSeconds = 0.30;
} // namespace

DualPlayerController::DualPlayerController()  = default;
DualPlayerController::~DualPlayerController() {
    Finalize();
}

void DualPlayerController::Initialize() {
    // Mp4Player は Media Foundation を要求する(MFStartup/MFShutdown は参照カウント制)。
    OriGine::WebCamera::StaticInitialize();
    mfInitialized_ = true;

    for (auto& slot : slots_) {
        slot.texture.Initialize();
    }
}

void DualPlayerController::Finalize() {
    for (auto& slot : slots_) {
        if (slot.player) {
            slot.player->Close();
            slot.player.reset();
        }
        slot.texture.Finalize();
    }
    if (mfInitialized_) {
        OriGine::WebCamera::StaticFinalize();
        mfInitialized_ = false;
    }
}

bool DualPlayerController::OpenSlot(int slot, const std::string& mp4Path) {
    if (slot < 0 || slot >= kSlotCount) {
        return false;
    }
    lastError_.clear();

    auto player = std::make_unique<OriGine::Mp4Player>();
    if (!player->Open(mp4Path)) {
        lastError_ = "failed to open: " + mp4Path + " (" + player->GetLastError() + ")";
        LOG_ERROR("DualPlayerController: {}", lastError_);
        return false;
    }

    Slot& s = slots_[slot];
    if (s.player) {
        s.player->Close();
    }
    s.player = std::move(player);
    s.path   = mp4Path;
    if (s.label.empty()) {
        s.label = fs::path(mp4Path).filename().string();
    }
    s.player->SetLoop(false);
    return true;
}

bool DualPlayerController::OpenSession(const std::string& sessionJsonPath) {
    lastError_.clear();

    SessionInfo session;
    std::string err;
    if (!ReadSessionManifest(sessionJsonPath, session, &err)) {
        lastError_ = err;
        LOG_ERROR("DualPlayerController: {}", err);
        return false;
    }

    const fs::path baseDir = fs::path(sessionJsonPath).parent_path();

    bool anyOpened = false;
    for (const auto& track : session.tracks) {
        const int slot = (track.kind == TrackKind::ScreenSystemAudio) ? 1 : 0;
        const std::string full = (baseDir / track.file).string();
        slots_[slot].label = track.label;
        if (OpenSlot(slot, full)) {
            anyOpened = true;
        }
    }
    if (!anyOpened) {
        lastError_ = "session had no playable track: " + sessionJsonPath;
        return false;
    }
    return true;
}

void DualPlayerController::CloseSlot(int slot) {
    if (slot < 0 || slot >= kSlotCount) {
        return;
    }
    Slot& s = slots_[slot];
    if (s.player) {
        s.player->Close();
        s.player.reset();
    }
    s.path.clear();
    s.label.clear();
}

bool DualPlayerController::AnyLoaded() const {
    for (const auto& s : slots_) {
        if (s.player && s.player->IsOpen()) {
            return true;
        }
    }
    return false;
}

int DualPlayerController::MasterSlot() const {
    int    master      = -1;
    double bestDuration = -1.0;
    for (int i = 0; i < kSlotCount; ++i) {
        const auto& s = slots_[i];
        if (s.player && s.player->IsOpen()) {
            const double d = s.player->GetDuration();
            if (d > bestDuration) {
                bestDuration = d;
                master       = i;
            }
        }
    }
    return master;
}

void DualPlayerController::Play() {
    for (auto& s : slots_) {
        if (s.player && s.player->IsOpen()) {
            s.player->Play();
        }
    }
}

void DualPlayerController::Pause() {
    for (auto& s : slots_) {
        if (s.player && s.player->IsOpen()) {
            s.player->Pause();
        }
    }
}

void DualPlayerController::TogglePlay() {
    if (IsPlaying()) {
        Pause();
    } else {
        Play();
    }
}

bool DualPlayerController::IsPlaying() const {
    const int master = MasterSlot();
    if (master < 0) {
        return false;
    }
    return slots_[master].player->IsPlaying();
}

void DualPlayerController::SeekMaster(double seconds) {
    for (auto& s : slots_) {
        if (s.player && s.player->IsOpen()) {
            const double clamped = std::clamp(seconds, 0.0, s.player->GetDuration());
            s.player->Seek(clamped);
        }
    }
}

double DualPlayerController::GetMasterDuration() const {
    const int master = MasterSlot();
    return master < 0 ? 0.0 : slots_[master].player->GetDuration();
}

double DualPlayerController::GetMasterPosition() const {
    const int master = MasterSlot();
    return master < 0 ? 0.0 : slots_[master].player->GetPosition();
}

void DualPlayerController::Update() {
    // 最新フレームをテクスチャへ転送。
    for (auto& s : slots_) {
        if (!s.player || !s.player->IsOpen()) {
            continue;
        }
        uint32_t w = 0;
        uint32_t h = 0;
        if (s.player->GetLatestFrame(s.frameBuffer, w, h) && !s.frameBuffer.empty()) {
            s.texture.Update(s.frameBuffer.data(), w, h);
            ++s.framesReceived;
        }
    }

    // ドリフト補正: 再生中のみ、マスター位置に追従側を合わせる。
    const int master = MasterSlot();
    if (master < 0 || !slots_[master].player->IsPlaying()) {
        return;
    }
    const double masterPos = slots_[master].player->GetPosition();
    for (int i = 0; i < kSlotCount; ++i) {
        if (i == master) {
            continue;
        }
        Slot& s = slots_[i];
        if (!s.player || !s.player->IsOpen()) {
            continue;
        }
        if (masterPos > s.player->GetDuration()) {
            continue; // 追従側はすでに終端 -> そのまま
        }
        if (std::abs(s.player->GetPosition() - masterPos) > kSyncThresholdSeconds) {
            s.player->Seek(masterPos);
        }
    }
}

DualPlayerController::SlotView DualPlayerController::GetSlotView(int slot) const {
    SlotView view;
    if (slot < 0 || slot >= kSlotCount) {
        return view;
    }
    const Slot& s = slots_[slot];
    if (s.player && s.player->IsOpen()) {
        view.loaded   = true;
        view.path     = s.path;
        view.label    = s.label;
        view.width    = s.player->GetWidth();
        view.height   = s.player->GetHeight();
        view.hasAudio = s.player->HasAudio();
        view.texture  = s.texture.IsValid() ? &s.texture : nullptr;
        view.textureValid   = s.texture.IsValid();
        view.framesReceived = s.framesReceived;
        view.playing        = s.player->IsPlaying();
        const auto dbg = s.player->GetDebugStats();
        view.videoPushed      = dbg.videoPushed;
        view.audioSubmitted   = dbg.audioSubmitted;
        view.readFailed       = dbg.readFailed;
        view.lastStreamIndex  = dbg.lastStreamIndex;
        view.videoStreamIndex = dbg.videoStreamIndex;
        view.audioStreamIndex = dbg.audioStreamIndex;
        view.positionSeconds  = dbg.positionSeconds;
    }
    return view;
}

} // namespace LogGuide
