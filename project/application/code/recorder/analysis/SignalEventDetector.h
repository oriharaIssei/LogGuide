#pragma once

/// stl
#include <cstdint>
#include <deque>
#include <vector>

/// externals
#include <nlohmann/json.hpp>

namespace LogGuide {

// =============================================================================
// SignalEventDetector
//
// LLM を使わず、ルール／信号処理でタイムラインイベントを検出する:
//   - 無発話区間（silence_start / silence_end）: 閾値（既定 60 秒）以上の無音。
//   - 音量スパイク（volume_spike）: 笑い声・ため息など非言語音の候補。
//     フレーム RMS のローリング統計（Welford）から z スコアで外れ値を拾う。
//   - 発話密度の急増（speech_density）: 一定窓に発話イベントが集中した場合。
//
// 検出したイベントは nlohmann::json として返し、呼び出し側が JSONL へ書く。
// 状態を跨いで持つため 1 セッション 1 インスタンスで使う。
// =============================================================================
class SignalEventDetector {
public:
    struct Config {
        uint32_t sampleRate         = 16000;
        uint32_t frameSamples       = 480;   // 30ms @ 16kHz
        float    volumeSpikeZ       = 3.0f;  // z スコアしきい値
        int64_t  spikeDebounceMs    = 1500;  // スパイク検出の最小間隔
        int64_t  silenceThresholdMs = 60000; // 無発話イベントの下限
        int64_t  densityWindowMs    = 20000; // 発話密度を測る窓
        int      densityBurstCount  = 5;     // 窓内この件数以上で density イベント
        int64_t  densityCooldownMs  = 20000; // density イベントの最小間隔
        int      spikeWarmupFrames  = 100;   // これだけ統計を貯めてからスパイク判定
    };

    explicit SignalEventDetector(const Config& config) : config_(config) {}

    // チャンク音声（16kHz モノ）を走査し、音量スパイクイベントを out へ追加する。
    void ProcessChunkAudio(int64_t chunkStartMs, const std::vector<float>& samples,
                           std::vector<nlohmann::json>& out);

    // 無音ランの状態更新。無発話区間の開始/終了イベントを out へ追加する。
    //   nowMs         : 現在（直近チャンク終端）のタイムライン時刻
    //   silenceRunMs  : 末尾から連続している無音の長さ
    //   speechResumed : このチャンクで発話が再開したか
    void UpdateSilence(int64_t nowMs, int64_t silenceRunMs, bool speechResumed,
                       std::vector<nlohmann::json>& out);

    // 発話イベント発生を通知し、密度急増イベントを out へ追加する。
    void OnSpeechEvent(int64_t timeMs, std::vector<nlohmann::json>& out);

private:
    Config config_;

    // 音量スパイク: フレーム RMS のオンライン統計（Welford）。
    int64_t rmsCount_ = 0;
    double  rmsMean_  = 0.0;
    double  rmsM2_    = 0.0;
    int64_t lastSpikeMs_ = -1;

    // 無発話区間の状態機械。
    bool    inSilence_     = false;
    int64_t silenceStartMs_ = 0;

    // 発話密度: 窓内の発話時刻。
    std::deque<int64_t> speechTimes_;
    int64_t lastDensityMs_ = -1;
};

} // namespace LogGuide
