#pragma once

/// stl
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

/// module
#include "analysis/TimelineJsonlReader.h"
#include "player/VideoTexture.h"

namespace OriGine {
class Mp4Player;
}

namespace LogGuide {

// =============================================================================
// DualPlayerController
//
// 録画で得られた 2 本の mp4(camera.mp4 / screen.mp4)を、1 つのタイムラインで
// 同期再生するコントローラ。各スロットが Mp4Player(映像+音声の同期デコード再生)と
// 表示用 VideoTexture を持つ。
//
// タイムライン同期方針:
//   - マスター = 開いているスロットのうち最長尺のもの。
//   - Play/Pause/Seek は両スロットへ一括適用。
//   - 各 Mp4Player は自前の音声クロックで進むため、フレーム毎にマスター位置と
//     追従側の位置を比較し、しきい値を超えてズレたら追従側を Seek で引き戻す。
// =============================================================================
class DualPlayerController {
public:
    static constexpr int kSlotCount = 2;

    DualPlayerController();
    ~DualPlayerController();

    void Initialize();
    void Finalize();

    // 毎フレーム呼ぶ: 最新フレームをテクスチャへ転送し、ドリフト補正を行う。
    void Update();

    // slot(0/1) に単一 mp4 を読み込む。失敗時 false。
    bool OpenSlot(int slot, const std::string& mp4Path);
    // session.json を読み、camera を slot0 / screen を slot1 に読み込む。
    bool OpenSession(const std::string& sessionJsonPath);
    void CloseSlot(int slot);

    // トランスポート(両スロットへ一括適用)。
    void Play();
    void Pause();
    void TogglePlay();
    bool IsPlaying() const;
    void SeekMaster(double seconds);

    double GetMasterDuration() const;
    double GetMasterPosition() const;

    // ---- UI 参照用 ----
    struct SlotView {
        bool               loaded = false;
        std::string        path;      // 読み込んだファイルパス
        std::string        label;     // "camera" / "screen" / ファイル名
        uint32_t           width  = 0;
        uint32_t           height = 0;
        bool               hasAudio = false;
        const VideoTexture* texture = nullptr;
        // ---- 診断用 ----
        bool               textureValid   = false; // GPU テクスチャが生成済みか
        uint64_t           framesReceived = 0;      // Mp4Player から取得できた映像フレーム数
        bool               playing        = false;  // このスロットが再生中か
        uint64_t           videoPushed     = 0;     // デコードされた映像サンプル数
        uint64_t           audioSubmitted  = 0;     // XAudio2 に送れた音声バッファ数
        uint64_t           readFailed      = 0;     // ReadSample 失敗回数
        uint32_t           lastStreamIndex = 0xFFFFFFFF;
        uint32_t           videoStreamIndex = 0;
        uint32_t           audioStreamIndex = 0;
        double             positionSeconds  = 0.0;
    };
    SlotView               GetSlotView(int slot) const;
    const std::string&     GetLastError() const { return lastError_; }

    // ---- AI 解析タイムライン ----
    // 開いた動画/セッションに対応する .jsonl があれば読み込まれている。
    const TimelineData& GetTimeline() const { return timeline_; }
    bool                HasTimeline() const { return !timeline_.Empty(); }

private:
    struct Slot {
        std::unique_ptr<OriGine::Mp4Player> player;
        VideoTexture                        texture;
        std::string                         path;
        std::string                         label;
        std::vector<uint8_t>                frameBuffer;
        uint64_t                            framesReceived = 0; // 診断: 取得できた映像フレーム数
    };

    int  MasterSlot() const;
    bool AnyLoaded() const;

    // media パス（例 screen.mp4）の隣に .jsonl があれば timeline_ へ読み込む。
    void LoadTimelineFor(const std::string& mediaPath);

    std::array<Slot, kSlotCount> slots_;
    bool                         mfInitialized_ = false;
    std::string                  lastError_;
    TimelineData                 timeline_;
};

} // namespace LogGuide
