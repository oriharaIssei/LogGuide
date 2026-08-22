#pragma once

/// stl
#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

/// module
#include "analysis/AnalysisConfig.h"

namespace LogGuide {

class TimelineJsonlWriter;
class VideoSignalDetector;

// =============================================================================
// VideoAnalysisPipeline
//
// 録画中の画面キャプチャフレームを受け取り、映像シグナルイベントを JSONL へ出力する。
// RecordingSystem が ScreenCapture のタップを OnFrame へ分岐する。
//
// 構成:
//   OnFrame（キャプチャスレッド）
//     → sampleFps までレート制限（大半のフレームは時刻チェックだけで戻る）
//     → BGRA をダウンスケールして輝度フレームへ（ボックス平均、CPU のみ）
//     → VideoSignalDetector で停滞/暗転/遷移/頻発を検出
//     → TimelineJsonlWriter へ追記（音声側と同じライタを共有する）
//
// ワーカースレッドは持たない。3fps で 160x90 への縮小と差分計算しかしないため、
// キャプチャスレッド上で直接処理しても録画を阻害しない（GPU は一切使わない）。
//
// 時刻の基準は Start() 時点の steady_clock。音声側はサンプル数から時刻を出すが、
// どちらも録画開始が原点なので、相関判定（数十秒粒度）に必要な精度は満たす。
// =============================================================================
class VideoAnalysisPipeline {
public:
    VideoAnalysisPipeline();
    ~VideoAnalysisPipeline();

    VideoAnalysisPipeline(const VideoAnalysisPipeline&)            = delete;
    VideoAnalysisPipeline& operator=(const VideoAnalysisPipeline&) = delete;

    // 設定を読み込む。video_analysis.enabled = false なら false を返し、以後何もしない。
    bool Initialize(const VideoAnalysisTuning& tuning, std::vector<std::string>* warnings = nullptr);

    // 音声側と共有するライタを受け取り、検出を開始する。録画開始と同時に呼ぶ。
    bool Start(std::shared_ptr<TimelineJsonlWriter> writer);

    // キャプチャスレッドから呼ばれる。bgra は 1 ピクセル 4 バイト（B,G,R,A）。
    void OnFrame(const uint8_t* bgra, uint32_t width, uint32_t height, uint32_t stride);

    // 録画停止時に呼ぶ。開いたままの停滞/暗転区間を閉じてライタを手放す。
    // 呼び出し前にキャプチャスレッドが停止していること（OnFrame と競合しない）。
    void Stop();

    bool IsEnabled() const { return enabled_; }
    bool IsRunning() const { return running_.load(); }

    // ---- 状態参照（UI 用: しきい値調整のために差分量の実測値を見せる） ----
    struct Status {
        bool     enabled       = false;
        bool     running       = false;
        uint64_t samples       = 0;
        uint64_t events        = 0;
        double   lastDiff      = 0.0;
    };
    Status GetStatus() const;

private:
    // BGRA フレームを downscaleWidth x N の輝度 [0,1] へ落とす（luma_ に書く）。
    void Downscale(const uint8_t* bgra, uint32_t width, uint32_t height, uint32_t stride);

    VideoAnalysisTuning tuning_;
    bool                enabled_ = false;

    std::shared_ptr<TimelineJsonlWriter>  writer_;
    std::unique_ptr<VideoSignalDetector>  detector_;

    // OnFrame（キャプチャスレッド）と Stop / GetStatus（UI スレッド）の排他。
    mutable std::mutex mutex_;
    std::atomic<bool>  running_{false};

    std::chrono::steady_clock::time_point startClock_{};
    int64_t nextSampleMs_ = 0;
    int64_t sampleIntervalMs_ = 333;
    int64_t lastTimeMs_ = 0;

    std::vector<float> luma_; // ダウンスケール後の輝度バッファ（毎回再利用）
    uint32_t           dstWidth_  = 0;
    uint32_t           dstHeight_ = 0;

    std::atomic<uint64_t> samples_{0};
    std::atomic<uint64_t> events_{0};
    std::atomic<double>   lastDiff_{0.0};
};

} // namespace LogGuide
