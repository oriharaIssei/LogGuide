#pragma once

/// stl
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

/// module
#include "analysis/AnalysisConfig.h"

namespace LogGuide {

class WhisperTranscriber;
class LocalLLM;
class TimelineJsonlWriter;
class SpeechChunkSplitter;
class SignalEventDetector;
class TranscriptClassifier;
class SessionSummarizer;

// =============================================================================
// AudioAnalysisPipeline
//
// 録画中の音声ストリームを受け取り、AI 解析タイムラインを JSONL へ出力する
// パイプラインの中枢。RecordingSystem がマイクの音声を OnAudio へ分岐する。
//
// 構成:
//   OnAudio（キャプチャスレッド）
//     → 16kHz モノへ変換して入力キューへ積むだけ（重い処理はしない）
//   ワーカースレッド
//     → キューから取り出し SpeechChunkSplitter でチャンク化
//     → whisper.cpp で文字起こし（speech イベント）
//     → SignalEventDetector で無発話/音量/密度（信号レベル）
//     → TranscriptClassifier で分類（意味レベル、LLM 有効時）
//     → TimelineJsonlWriter へ追記
//
// GPU 負荷制御（三段縮退）もワーカー内で行う:
//   1. チャンク処理の合間にスリープ（推論優先度を下げる）
//   2. RTF 逼迫が続いたら whisper を large→medium へフォールバック
//   3. 最終手段: 解析を止め録音のみ継続（音声を退避し、終了後にバッチ解析）
//   縮退の発生は degradation イベントとして JSONL に記録する。
// =============================================================================
class AudioAnalysisPipeline {
public:
    AudioAnalysisPipeline();
    ~AudioAnalysisPipeline();

    AudioAnalysisPipeline(const AudioAnalysisPipeline&)            = delete;
    AudioAnalysisPipeline& operator=(const AudioAnalysisPipeline&) = delete;

    // 設定を読み込み、モデルをロードする（重い）。録画開始前に 1 回呼ぶ。
    // 戻り値は「解析が何らかの形で動作可能か」。文字起こしが無効でも
    // 信号レベルのみで動くなら true。warnings に無効化理由を UTF-8 で積む。
    bool Initialize(const AnalysisConfig& config, std::vector<std::string>* warnings = nullptr);

    // タイムラインライタ（映像側と共有する）を受け取り、ワーカースレッドを起動する。
    // 録画開始と同時に呼ぶ。meta 行の書き出しは呼び出し側の責務。
    bool Start(std::shared_ptr<TimelineJsonlWriter> writer);

    // 終了処理（ワーカ join → バッチ解析 → ライタ Close）の完了後に、その
    // バックグラウンドスレッド上で呼ばれる。引数は書き終えた JSONL のパス。
    // クロスモーダル相関のようなタイムライン全体を要する後処理をここに繋ぐ。
    void SetOnFinalized(std::function<void(const std::string&)> callback);

    // マイク等のキャプチャスレッドから呼ばれる。samples は [-1,1] のインターリーブ float。
    void OnAudio(const float* samples, uint32_t frameCount, uint32_t channels, uint32_t sampleRate);

    // 録画停止時に呼ぶ。重い終了処理（残チャンク処理・退避音声のバッチ解析・LLM 要約）は
    // 別スレッドで走らせ、呼び出し元（UI スレッド）はブロックしない。
    void Stop();

    bool IsRunning() const { return running_.load(); }
    // 終了処理（残チャンク・バッチ解析）がバックグラウンドで進行中か。
    bool IsFinalizing() const { return finalizing_.load(); }

    // 既存の .jsonl（ビューアで開いているタイムライン）を読み、その文字起こしから
    // セッション要約を生成して summary イベントを追記する。録画停止時の自動実行ではなく、
    // ユーザーがボタンで明示的に呼ぶための on-demand 版。別スレッドで走り UI をブロックしない。
    // summarize モデル無効・録画中・要約中・文字起こし空なら false。
    bool SummarizeExisting(const std::string& jsonlPath);
    bool IsSummarizing() const { return summarizing_.load(); }
    bool CanSummarize() const; // summarize モデルが有効で、今すぐ実行できるか

    // ---- 状態参照（UI 用） ----
    struct Status {
        bool     transcribeEnabled = false;
        bool     classifyEnabled   = false;
        bool     summarizeEnabled  = false;
        bool     finalizing        = false; // 終了処理（バッチ解析・要約）が進行中
        int      degradeLevel      = 0;   // 0=通常 1=medium 2=解析停止
        double   lastRtf           = 0.0;
        uint64_t chunksProcessed   = 0;
        uint64_t speechEvents      = 0;
        uint64_t queuedSamples     = 0;
    };
    Status GetStatus() const;

private:
    void WorkerLoop();
    // Stop から起動される終了処理本体（別スレッドで実行）。
    void FinalizeStop();
    // 1 チャンクを処理し JSONL へ書く。GPU 縮退判定もここで行う。
    void ProcessChunk(const std::vector<float>& mono16k, int64_t startMs, int64_t durationMs,
                      bool hadSpeech, int64_t silenceRunMs);
    // large→medium 縮退を試みる。medium 未設定なら false。
    bool TryFallbackToMedium(int64_t nowMs);
    // 解析停止（第 3 段）。以降 OnAudio は退避のみ、ワーカーは要約待ちへ。
    void EnterAnalysisStopped(int64_t nowMs);
    // 終了後バッチ解析: 退避した音声をまとめて処理する。
    void RunBatchAnalysis();

    // 16kHz モノへの変換（キャプチャスレッドで実行）。
    void ToMono16k(const float* samples, uint32_t frameCount, uint32_t channels,
                   uint32_t sampleRate, std::vector<float>& out) const;

    AnalysisConfig config_;

    std::unique_ptr<WhisperTranscriber> whisper_;
    std::unique_ptr<LocalLLM>            classifyLlm_;
    std::unique_ptr<LocalLLM>            summarizeLlm_;
    std::shared_ptr<TimelineJsonlWriter> writer_; // 映像解析パイプラインと共有する
    std::unique_ptr<SpeechChunkSplitter> splitter_;
    std::unique_ptr<SignalEventDetector> signals_;
    std::unique_ptr<TranscriptClassifier> classifier_;
    std::unique_ptr<SessionSummarizer>   summarizer_;

    bool transcribeEnabled_ = false;

    // 入力キュー（キャプチャスレッド → ワーカー）。16kHz モノ。
    mutable std::mutex      queueMutex_;
    std::condition_variable queueCv_;
    std::deque<float>       inputQueue_;

    std::thread        worker_;
    std::thread        finalizeThread_;
    std::thread        summarizeThread_;
    std::atomic<bool>  running_{false};
    std::atomic<bool>  finalizing_{false};
    std::atomic<bool>  summarizing_{false};
    std::atomic<bool>  stopRequested_{false};

    // GPU 縮退状態。
    std::atomic<int>   degradeLevel_{0}; // 0 通常 / 1 medium / 2 停止
    int                highRtfStreak_ = 0;

    // 第 3 段縮退時に退避する音声（16kHz モノ）と、その開始サンプル位置。
    std::mutex         spillMutex_;
    std::vector<float> spilledAudio_;
    bool               analysisStopped_ = false;

    // 文字起こし全文（要約用に蓄積）。
    std::mutex         transcriptMutex_;
    std::string        fullTranscript_;
    std::vector<std::string> eventDigest_;

    // 統計。
    std::atomic<uint64_t> chunksProcessed_{0};
    std::atomic<uint64_t> speechEvents_{0};
    std::atomic<double>   lastRtf_{0.0};

    std::string jsonlPath_;   // 終了コールバックへ渡すために Start で控える
    std::function<void(const std::string&)> onFinalized_;
};

} // namespace LogGuide
