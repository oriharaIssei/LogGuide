#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct whisper_context;

namespace LogGuide {

struct WhisperToken {
    std::string text;
    float probability;
    float voiceLength;
    int64_t t0;
    int64_t t1;
};

struct WhisperSegment {
    std::string text;
    int64_t t0;
    int64_t t1;
    std::vector<WhisperToken> tokens;
};

struct WhisperResult {
    std::string fullText;
    std::vector<WhisperSegment> segments;
};

/// <summary>
/// 認識のチューニング用パラメータ。実機・マイクに合わせて調整する。
/// 既定値は whisper.cpp の標準値に準拠（過度に厳しくしない）。
/// </summary>
struct WhisperParams {
    bool vadEnabled       = true;   // Silero VAD を使うか（切ると全音声を whisper に渡す）
    float vadThreshold    = 0.5f;   // VAD のしきい値（高いほど厳しい / Silero標準0.5）
    float noSpeechThold   = 0.6f;   // これ未満の no_speech 確率なら発話扱い（標準0.6）
    float logprobThold    = -1.0f;  // 平均logprobがこれ未満なら失敗扱い（標準-1.0）
    float entropyThold    = 2.4f;   // 繰り返し検出のしきい値（標準2.4）
    float temperature     = 0.0f;   // 初期サンプリング温度
    float temperatureInc  = 0.2f;   // フォールバック温度の増分（0で無効化＝復帰しない）
    float minAvgProb      = 0.2f;   // アプリ側: 平均トークン確率がこれ未満のセグメントを捨てる
    float inputGain       = 1.0f;   // 入力音声に掛けるゲイン（小さい入力対策）
    bool filterBracketed  = true;   // 括弧・♪を含むセグメント(効果音注釈)を捨てる
};

class WhisperTranscriber {
public:
    WhisperTranscriber();
    ~WhisperTranscriber();

    bool LoadModel(const std::string& modelPath, int nThreads = 0);
    void UnloadModel();
    bool IsModelLoaded() const;

    void SetLanguage(const std::string& lang);
    void SetInitialPrompt(const std::string& prompt);
    void SetBeamSize(int beamSize);
    void SetVadModelPath(const std::string& path);

    /// <summary>
    /// 固有名詞などの語彙を設定する。initial_prompt に合成され、Whisper の
    /// デコードをその綴りへ寄せる（人名・グループ名の認識率向上）。
    /// </summary>
    void SetVocabulary(const std::vector<std::string>& words) { vocabulary_ = words; }
    const std::vector<std::string>& GetVocabulary() const { return vocabulary_; }

    /// <summary>チューニング用パラメータへの参照（UI から編集可能）。</summary>
    WhisperParams& Params() { return params_; }
    const WhisperParams& Params() const { return params_; }

    void PushAudio(const float* samples, uint32_t frameCount, uint32_t channels, uint32_t sampleRate);

    bool Transcribe();

    std::string GetResult() const;
    WhisperResult GetDetailedResult() const;

    /// <summary>直近 Transcribe() の実測 RTF（推論壁時計時間 / 音声長）。未計測なら 0。</summary>
    double GetLastRtf() const;
    /// <summary>直近 Transcribe() の推論に要した壁時計時間（秒）。</summary>
    double GetLastInferSec() const;
    /// <summary>直近 Transcribe() に渡した音声の長さ（秒）。</summary>
    double GetLastAudioSec() const;

    void ClearAudio();

    size_t GetAudioSampleCount() const;
    std::vector<float> GetAudioSnapshot() const;

private:
    void Resample(const float* src, uint32_t srcFrames, uint32_t srcRate, std::vector<float>& dst);
    void MixToMono(const float* src, uint32_t frameCount, uint32_t channels, std::vector<float>& dst);

    whisper_context* ctx_ = nullptr;
    int nThreads_ = 4;
    std::string language_ = "ja";
    std::string initialPrompt_ = "以下は日本語の会話です。";
    std::vector<std::string> vocabulary_;
    int beamSize_ = 5;
    std::string vadModelPath_;
    WhisperParams params_;

    mutable std::mutex audioMutex_;
    std::vector<float> audioBuffer_;

    mutable std::mutex resultMutex_;
    WhisperResult result_;

    // 直近 Transcribe() の実測タイミング（RTF 用）
    mutable std::mutex timingMutex_;
    double lastInferSec_ = 0.0;
    double lastAudioSec_ = 0.0;
    double lastRtf_       = 0.0;
};

} // namespace LogGuide
