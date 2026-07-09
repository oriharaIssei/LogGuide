#pragma once

/// stl
#include <cstdint>
#include <vector>

namespace LogGuide {

// =============================================================================
// SpeechChunkSplitter
//
// 16kHz モノラル音声ストリームを、発話区間の切れ目で 10〜30 秒のチャンクへ
// 分割するストリーミング分割器。低レイテンシは不要なのでスループット重視で
// チャンクを大きく取り、whisper への投入回数を抑える。
//
// 判定はフレーム単位の RMS エネルギーによる簡易 VAD:
//   - RMS がしきい値超え = 発話、下回る = 無音。
//   - 「最小長を超えていて、かつ無音が gap 秒続いた」ら区切る（自然な切れ目）。
//   - それでも切れなければ最大長で強制的に区切る。
//
// 無音ランの長さ（GetSilenceRunMs）は SignalEventDetector が無発話区間イベントの
// 検出に流用する（VAD 境界検出が無発話検出を兼ねる、という設計）。
// whisper 内蔵の Silero VAD が最終的な発話フィルタを担うため、ここは粗くてよい。
// =============================================================================
class SpeechChunkSplitter {
public:
    struct Config {
        uint32_t sampleRate   = 16000;
        int      minChunkSec  = 10;
        int      maxChunkSec  = 30;
        float    gapSec       = 0.7f;   // この長さ無音が続いたら区切り候補
        float    rmsThreshold = 0.01f;  // これ超えで発話とみなす
        uint32_t frameSamples = 480;    // RMS を測るフレーム長（16kHz で 30ms）
    };

    // 1 チャンク分の切り出し結果。
    struct Chunk {
        std::vector<float> samples;   // 16kHz モノラル PCM
        int64_t            startMs;   // セッション先頭からの開始時刻
        int64_t            durationMs;
        bool               hadSpeech; // RMS 上でいちどでも発話を検出したか
    };

    explicit SpeechChunkSplitter(const Config& config) : config_(config) {}

    // 音声を供給する。チャンク境界に達したら outChunk へ書き出して true を返す。
    // 1 回の Feed で複数チャンクは返さない（余りは内部に保持され次回以降で返る）。
    bool Feed(const float* samples, size_t count, Chunk& outChunk);

    // ストリーム終端で呼ぶ。バッファに残った音声を最終チャンクとして取り出す。
    bool Flush(Chunk& outChunk);

    // 現在の連続無音長（ミリ秒）。無発話区間イベント検出に使う。
    int64_t GetSilenceRunMs() const;

    // これまでに消費した総サンプル数（サンプルクロックの現在時刻算出用）。
    int64_t GetConsumedSamples() const { return consumedSamples_; }

private:
    int64_t SamplesToMs(int64_t samples) const;
    void    EmitChunk(Chunk& outChunk);

    Config             config_;
    std::vector<float> buffer_;          // 現在のチャンクに溜めている音声
    int64_t            chunkStartSample_ = 0; // buffer_ 先頭のサンプル位置
    int64_t            consumedSamples_  = 0; // Feed で受け取った累計サンプル数
    int64_t            silenceRunSamples_ = 0; // 連続無音サンプル数（末尾から）
    bool               chunkHadSpeech_   = false;
    // フレーム境界をまたぐ端数サンプルを次回に持ち越すためのバッファ。
    std::vector<float> frameRemainder_;
};

} // namespace LogGuide
