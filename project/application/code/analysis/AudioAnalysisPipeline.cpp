#include "analysis/AudioAnalysisPipeline.h"

/// module
#include "analysis/LocalLLM.h"
#include "analysis/SessionSummarizer.h"
#include "analysis/SignalEventDetector.h"
#include "analysis/SpeechChunkSplitter.h"
#include "analysis/TimelineEvent.h"
#include "analysis/TimelineJsonlReader.h"
#include "analysis/TimelineJsonlWriter.h"
#include "analysis/TranscriptClassifier.h"
#include "analysis/WhisperTranscriber.h"

/// engine
#include "logger/Logger.h"

/// stl
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>

namespace fs = std::filesystem;

namespace LogGuide {

namespace {

constexpr uint32_t kTargetRate = 16000; // whisper のサンプルレート

bool FileExists(const std::string& path) {
    if (path.empty()) {
        return false;
    }
    std::error_code ec;
    return fs::exists(path, ec);
}

// 要約プロンプトに入れるタイムスタンプ（m:ss）。
std::string FormatStamp(int64_t timeMs) {
    const int totalSec = static_cast<int>(timeMs / 1000);
    char buf[16] = {};
    std::snprintf(buf, sizeof(buf), "%d:%02d", totalSec / 60, totalSec % 60);
    return buf;
}

bool IsMeaningfulTag(const std::string& tag) {
    return !tag.empty() && tag != "none" && tag != "unclassified";
}

// タイムライン全体から、要約 LLM に渡すイベント要点を組み立てる。
// 合成イベント（stuck_candidate 等）を優先して含めることで、「無発話が 3 回」ではなく
// 「詰まっていた可能性のある区間が 3 箇所（各タイムスタンプ）」と報告させる。
std::string BuildEventDigest(const TimelineData& timeline) {
    // unexpected_reaction が指す発話本文を引くための索引。
    auto findSpeech = [&timeline](int64_t timeMs) -> const TimelineEntry* {
        for (const auto& e : timeline.entries) {
            if (e.type == "speech" && e.timeMs == timeMs) {
                return &e;
            }
        }
        return nullptr;
    };

    std::string digest;
    for (const auto& e : timeline.entries) {
        const std::string stamp = "[" + FormatStamp(e.timeMs) + "] ";

        if (e.type == "stuck_candidate") {
            digest += stamp + "詰まり候補: 無発話と画面停滞が " +
                      std::to_string(e.overlapMs / 1000) + " 秒重なった\n";
        } else if (e.type == "unexpected_reaction") {
            std::string what = "想定外の反応: 画面が切り替わった直後の発話";
            for (const auto& ref : e.sources) {
                if (ref.type != "speech") {
                    continue;
                }
                if (const TimelineEntry* sp = findSpeech(ref.timeMs)) {
                    what += " [" + sp->tag + "] " + sp->text;
                }
            }
            digest += stamp + what + "\n";
        } else if (e.type == "focus_likely") {
            digest += stamp + "集中していた可能性: " + std::to_string(e.durationMs / 1000) +
                      " 秒の無発話だが画面は動いていた（詰まりではない）\n";
        } else if (e.type == "silence_end" && !e.suppressed && e.durationMs > 0) {
            digest += stamp + "無発話 " + std::to_string(e.durationMs / 1000) + " 秒\n";
        } else if (e.type == "speech" && IsMeaningfulTag(e.tag)) {
            digest += stamp + "[" + e.tag + "] " + e.text + "\n";
        }
    }
    return digest;
}

} // namespace

AudioAnalysisPipeline::AudioAnalysisPipeline()  = default;

AudioAnalysisPipeline::~AudioAnalysisPipeline() {
    // 破棄前に、走っている解析・終了処理を必ず完了させる（スレッドが this を参照するため）。
    Stop();
    if (finalizeThread_.joinable()) {
        finalizeThread_.join();
    }
    if (summarizeThread_.joinable()) {
        summarizeThread_.join();
    }
    // Stop が呼ばれず worker が走っていた場合の保険。
    stopRequested_.store(true);
    queueCv_.notify_all();
    if (worker_.joinable()) {
        worker_.join();
    }
}

bool AudioAnalysisPipeline::Initialize(const AnalysisConfig& config, std::vector<std::string>* warnings) {
    config_ = config;

    if (!config_.tuning.enabled) {
        if (warnings) {
            warnings->push_back("analysis disabled in logguide.toml");
        }
        return false;
    }

    auto warn = [&](const std::string& msg) {
        LOG_WARN("AudioAnalysisPipeline: {}", msg);
        if (warnings) {
            warnings->push_back(msg);
        }
    };

    // --- whisper（文字起こし） ---
    transcribeEnabled_ = false;
    if (config_.whisper.model.empty()) {
        warn("whisper model not configured; transcription disabled (signal-level only)");
    } else if (!FileExists(config_.whisper.model)) {
        warn("whisper model file not found: " + config_.whisper.model + "; transcription disabled");
    } else {
        whisper_ = std::make_unique<WhisperTranscriber>();
        if (!whisper_->LoadModel(config_.whisper.model)) {
            warn("failed to load whisper model: " + config_.whisper.model + "; transcription disabled");
            whisper_.reset();
        } else {
            whisper_->SetLanguage(config_.whisper.language);
            if (FileExists(config_.whisper.vadModel)) {
                whisper_->SetVadModelPath(config_.whisper.vadModel);
            }
            transcribeEnabled_ = true;
        }
    }

    // --- classify（意味レベル検出） ---
    if (!config_.classify.model.empty()) {
        if (!FileExists(config_.classify.model)) {
            warn("classify model file not found: " + config_.classify.model + "; classification disabled");
        } else {
            classifyLlm_ = std::make_unique<LocalLLM>();
            if (!classifyLlm_->LoadModel(config_.classify.model, config_.classify.gpuLayers,
                                         config_.classify.contextSize)) {
                warn("failed to load classify model; classification disabled");
                classifyLlm_.reset();
            } else {
                classifyLlm_->SetMaxTokens(config_.classify.maxTokens);
                classifier_ = std::make_unique<TranscriptClassifier>(classifyLlm_.get());
            }
        }
    }

    // --- summarize（録画終了後バッチ要約） ---
    if (!config_.summarize.model.empty()) {
        if (!FileExists(config_.summarize.model)) {
            warn("summarize model file not found: " + config_.summarize.model + "; summarize disabled");
        } else if (classifyLlm_ && config_.summarize.model == config_.classify.model) {
            // classify と同一モデルなら再ロードせず共用する（VRAM 節約）。
            summarizeLlm_ = nullptr;
            summarizer_   = std::make_unique<SessionSummarizer>(classifyLlm_.get());
        } else {
            summarizeLlm_ = std::make_unique<LocalLLM>();
            if (!summarizeLlm_->LoadModel(config_.summarize.model, config_.summarize.gpuLayers,
                                          config_.summarize.contextSize)) {
                warn("failed to load summarize model; summarize disabled");
                summarizeLlm_.reset();
            } else {
                summarizeLlm_->SetMaxTokens(config_.summarize.maxTokens);
                summarizer_ = std::make_unique<SessionSummarizer>(summarizeLlm_.get());
            }
        }
    }

    LOG_INFO("AudioAnalysisPipeline: initialized (transcribe={}, classify={}, summarize={})",
             transcribeEnabled_, classifier_ != nullptr,
             summarizer_ != nullptr && summarizer_->IsAvailable());
    return true;
}

void AudioAnalysisPipeline::SetOnFinalized(std::function<void(const std::string&)> callback) {
    onFinalized_ = std::move(callback);
}

bool AudioAnalysisPipeline::Start(std::shared_ptr<TimelineJsonlWriter> writer) {
    // 前セッションの終了処理（バッチ解析・相関）がまだ走っていれば、ここで完了を待つ。
    // running_ を落とすのはその終了処理なので、running_ の判定より先に join する。
    // これにより finalizeThread_ は非 joinable になり、次の Stop で安全に再代入できる。
    if (finalizeThread_.joinable()) {
        finalizeThread_.join();
    }
    if (running_.load() || !writer || !writer->IsOpen()) {
        return false;
    }

    writer_    = std::move(writer);
    jsonlPath_ = writer_->GetPath();

    // チャンク分割・信号検出をセットアップ。
    SpeechChunkSplitter::Config sc;
    sc.sampleRate  = kTargetRate;
    sc.minChunkSec = config_.tuning.chunkMinSec;
    sc.maxChunkSec = config_.tuning.chunkMaxSec;
    splitter_ = std::make_unique<SpeechChunkSplitter>(sc);

    SignalEventDetector::Config sig;
    sig.sampleRate         = kTargetRate;
    sig.volumeSpikeZ       = config_.tuning.volumeSpikeZ;
    sig.silenceThresholdMs = static_cast<int64_t>(config_.tuning.silenceThresholdSec) * 1000;
    sig.densityWindowMs    = static_cast<int64_t>(config_.tuning.densityWindowSec) * 1000;
    sig.densityBurstCount  = config_.tuning.densityBurstCount;
    signals_ = std::make_unique<SignalEventDetector>(sig);

    // 状態リセット。
    { std::lock_guard<std::mutex> l(queueMutex_); inputQueue_.clear(); }
    { std::lock_guard<std::mutex> l(spillMutex_); spilledAudio_.clear(); analysisStopped_ = false; }
    { std::lock_guard<std::mutex> l(transcriptMutex_); fullTranscript_.clear(); eventDigest_.clear(); }
    degradeLevel_.store(0);
    highRtfStreak_ = 0;
    chunksProcessed_.store(0);
    speechEvents_.store(0);
    lastRtf_.store(0.0);

    stopRequested_.store(false);
    running_.store(true);
    worker_ = std::thread(&AudioAnalysisPipeline::WorkerLoop, this);

    LOG_INFO("AudioAnalysisPipeline: started -> {}", jsonlPath_);
    return true;
}

void AudioAnalysisPipeline::ToMono16k(const float* samples, uint32_t frameCount, uint32_t channels,
                                      uint32_t sampleRate, std::vector<float>& out) const {
    // ダウンミックス（モノ化）。
    std::vector<float> mono(frameCount);
    if (channels <= 1) {
        mono.assign(samples, samples + frameCount);
    } else {
        for (uint32_t i = 0; i < frameCount; ++i) {
            float sum = 0.0f;
            for (uint32_t ch = 0; ch < channels; ++ch) {
                sum += samples[i * channels + ch];
            }
            mono[i] = sum / static_cast<float>(channels);
        }
    }

    // 16kHz へ線形リサンプル。
    if (sampleRate == kTargetRate) {
        out = std::move(mono);
        return;
    }
    const double ratio = static_cast<double>(kTargetRate) / static_cast<double>(sampleRate);
    const uint32_t dstFrames = static_cast<uint32_t>(std::ceil(frameCount * ratio));
    out.resize(dstFrames);
    for (uint32_t i = 0; i < dstFrames; ++i) {
        const double srcPos = i / ratio;
        const uint32_t idx  = static_cast<uint32_t>(srcPos);
        const float frac    = static_cast<float>(srcPos - idx);
        if (idx + 1 < frameCount) {
            out[i] = mono[idx] * (1.0f - frac) + mono[idx + 1] * frac;
        } else if (idx < frameCount) {
            out[i] = mono[idx];
        } else {
            out[i] = 0.0f;
        }
    }
}

void AudioAnalysisPipeline::OnAudio(const float* samples, uint32_t frameCount, uint32_t channels,
                                    uint32_t sampleRate) {
    if (!running_.load() || frameCount == 0 || channels == 0) {
        return;
    }
    std::vector<float> mono16k;
    ToMono16k(samples, frameCount, channels, sampleRate, mono16k);
    if (mono16k.empty()) {
        return;
    }

    // 第 3 段縮退中は解析キューに積まず、終了後バッチ用に退避する。
    if (degradeLevel_.load() >= 2) {
        std::lock_guard<std::mutex> l(spillMutex_);
        spilledAudio_.insert(spilledAudio_.end(), mono16k.begin(), mono16k.end());
        return;
    }

    {
        std::lock_guard<std::mutex> l(queueMutex_);
        inputQueue_.insert(inputQueue_.end(), mono16k.begin(), mono16k.end());
    }
    queueCv_.notify_one();
}

void AudioAnalysisPipeline::WorkerLoop() {
    std::vector<float> batch;
    SpeechChunkSplitter::Chunk chunk;

    while (true) {
        {
            std::unique_lock<std::mutex> lock(queueMutex_);
            queueCv_.wait_for(lock, std::chrono::milliseconds(200), [&] {
                return !inputQueue_.empty() || stopRequested_.load();
            });
            if (!inputQueue_.empty()) {
                batch.assign(inputQueue_.begin(), inputQueue_.end());
                inputQueue_.clear();
            } else {
                batch.clear();
            }
        }

        // 第 3 段縮退に入ったらワーカーは離脱（残りは Stop 後にバッチ処理）。
        if (degradeLevel_.load() >= 2) {
            break;
        }

        if (!batch.empty()) {
            // Feed は入力を全消費し、最大 1 チャンクを返す（残りは splitter 内部に保持）。
            if (splitter_->Feed(batch.data(), batch.size(), chunk)) {
                ProcessChunk(chunk.samples, chunk.startMs, chunk.durationMs,
                             chunk.hadSpeech, splitter_->GetSilenceRunMs());
            }
            // バックログが溜まっていれば、内部バッファ分を追加チャンクとして出し切る。
            while (degradeLevel_.load() < 2 && splitter_->Feed(nullptr, 0, chunk)) {
                ProcessChunk(chunk.samples, chunk.startMs, chunk.durationMs,
                             chunk.hadSpeech, splitter_->GetSilenceRunMs());
            }
        }

        if (stopRequested_.load()) {
            std::lock_guard<std::mutex> l(queueMutex_);
            if (inputQueue_.empty()) {
                break;
            }
        }
    }

    // 通常停止: 残りバッファを最終チャンクとして処理する。
    if (degradeLevel_.load() < 2) {
        if (splitter_->Flush(chunk)) {
            ProcessChunk(chunk.samples, chunk.startMs, chunk.durationMs,
                         chunk.hadSpeech, splitter_->GetSilenceRunMs());
        }
    }
}

void AudioAnalysisPipeline::ProcessChunk(const std::vector<float>& mono16k, int64_t startMs,
                                         int64_t durationMs, bool hadSpeech, int64_t silenceRunMs) {
    std::vector<nlohmann::json> events;

    // --- 信号レベル検出（LLM 不要） ---
    signals_->ProcessChunkAudio(startMs, mono16k, events);
    const int64_t chunkEndMs = startMs + durationMs;
    signals_->UpdateSilence(chunkEndMs, silenceRunMs, hadSpeech, events);

    // --- 文字起こし（whisper） ---
    if (transcribeEnabled_ && whisper_ && hadSpeech) {
        whisper_->ClearAudio();
        whisper_->PushAudio(mono16k.data(), static_cast<uint32_t>(mono16k.size()), 1, kTargetRate);
        if (whisper_->Transcribe()) {
            const double rtf = whisper_->GetLastRtf();
            lastRtf_.store(rtf);

            WhisperResult res = whisper_->GetDetailedResult();
            for (const auto& seg : res.segments) {
                if (seg.text.empty()) {
                    continue;
                }
                // whisper のセグメント時刻は 10ms 単位、チャンク先頭からの相対。
                const int64_t segMs = startMs + seg.t0 * 10;

                std::string tag;
                double conf = 0.0;
                if (classifier_ && classifier_->IsAvailable()) {
                    TranscriptClassifier::Result c = classifier_->Classify(seg.text);
                    tag  = c.tag;
                    conf = c.confidence;
                }
                events.push_back(MakeSpeechEvent(segMs, seg.text, tag, conf));
                speechEvents_.fetch_add(1);
                signals_->OnSpeechEvent(segMs, events);

                {
                    std::lock_guard<std::mutex> l(transcriptMutex_);
                    fullTranscript_ += seg.text;
                    fullTranscript_ += "\n";
                    if (!tag.empty() && tag != "none" && tag != "unclassified") {
                        eventDigest_.push_back("[" + tag + "] " + seg.text);
                    }
                }
            }

            // --- GPU 縮退判定（RTF が閾値を超え続けたら段階的に縮退） ---
            if (rtf > config_.tuning.rtfFallbackThreshold) {
                ++highRtfStreak_;
            } else {
                highRtfStreak_ = 0;
            }
            const int level = degradeLevel_.load();
            if (level == 0 && highRtfStreak_ >= 3) {
                if (TryFallbackToMedium(chunkEndMs)) {
                    highRtfStreak_ = 0;
                } else if (highRtfStreak_ >= 6) {
                    EnterAnalysisStopped(chunkEndMs);
                }
            } else if (level == 1 && highRtfStreak_ >= 3) {
                EnterAnalysisStopped(chunkEndMs);
            }
        }
    }

    // --- JSONL へ書き出し（時刻順に整列） ---
    std::sort(events.begin(), events.end(), [](const nlohmann::json& a, const nlohmann::json& b) {
        return a.value("time_ms", int64_t{0}) < b.value("time_ms", int64_t{0});
    });
    for (const auto& e : events) {
        writer_->Write(e);
    }

    chunksProcessed_.fetch_add(1);

    // 第 1 段縮退: チャンク処理の合間にスリープし、推論の優先度を下げる。
    if (config_.tuning.chunkSleepMs > 0 && degradeLevel_.load() < 2) {
        std::this_thread::sleep_for(std::chrono::milliseconds(config_.tuning.chunkSleepMs));
    }
}

bool AudioAnalysisPipeline::TryFallbackToMedium(int64_t nowMs) {
    if (config_.whisper.modelMedium.empty() || !FileExists(config_.whisper.modelMedium)) {
        return false;
    }
    LOG_WARN("AudioAnalysisPipeline: GPU pressure -> falling back to medium whisper model");
    if (!whisper_->LoadModel(config_.whisper.modelMedium)) {
        LOG_ERROR("AudioAnalysisPipeline: failed to load medium model; keeping large");
        return false;
    }
    whisper_->SetLanguage(config_.whisper.language);
    if (FileExists(config_.whisper.vadModel)) {
        whisper_->SetVadModelPath(config_.whisper.vadModel);
    }
    degradeLevel_.store(1);
    if (writer_) {
        writer_->Write(MakeDegradationEvent(nowMs, "gpu_pressure", "fallback_medium"));
    }
    return true;
}

void AudioAnalysisPipeline::EnterAnalysisStopped(int64_t nowMs) {
    LOG_WARN("AudioAnalysisPipeline: sustained GPU pressure -> stopping live analysis (batch after recording)");
    {
        std::lock_guard<std::mutex> l(spillMutex_);
        analysisStopped_ = true;
    }
    degradeLevel_.store(2);
    if (writer_) {
        writer_->Write(MakeDegradationEvent(nowMs, "gpu_pressure", "stop_analysis"));
    }
}

void AudioAnalysisPipeline::Stop() {
    if (!running_.load() || finalizing_.load()) {
        return;
    }
    stopRequested_.store(true);
    queueCv_.notify_all();

    // 終了処理（ワーカ join → 退避音声のバッチ解析 → writer close）は別スレッドで実行し、
    // UI スレッドを固めない。要約はここでは行わない（ビューアのボタンで on-demand 実行）。
    // 直前の終了スレッドは Start 側で join 済みなので、ここでの再代入は安全。
    finalizing_.store(true);
    finalizeThread_ = std::thread(&AudioAnalysisPipeline::FinalizeStop, this);
}

void AudioAnalysisPipeline::FinalizeStop() {
    if (worker_.joinable()) {
        worker_.join();
    }

    // 第 3 段縮退で退避した音声があれば、GPU が空いた今バッチ解析する。
    // （これは文字起こしの完成であり要約ではない。要約はユーザーがボタンで実行する。）
    RunBatchAnalysis();

    if (writer_) {
        writer_->Close();
        writer_.reset();
    }
    LOG_INFO("AudioAnalysisPipeline: stopped (chunks={}, speech={})",
             chunksProcessed_.load(), speechEvents_.load());

    // タイムラインが確定した後にクロスモーダル相関を回す。まだ finalizing_ を
    // 立てたままにしておき、相関中も UI が「解析処理中」と表示できるようにする。
    if (onFinalized_ && !jsonlPath_.empty()) {
        onFinalized_(jsonlPath_);
    }

    running_.store(false);
    finalizing_.store(false);
}

void AudioAnalysisPipeline::RunBatchAnalysis() {
    std::vector<float> spill;
    {
        std::lock_guard<std::mutex> l(spillMutex_);
        if (!analysisStopped_ || spilledAudio_.empty()) {
            return;
        }
        spill.swap(spilledAudio_);
    }
    if (!transcribeEnabled_ || !whisper_) {
        return;
    }

    LOG_INFO("AudioAnalysisPipeline: batch-analyzing {} spilled samples", spill.size());

    // GPU が空いたので large へ戻す（品質優先）。
    if (!config_.whisper.model.empty() && FileExists(config_.whisper.model)) {
        whisper_->LoadModel(config_.whisper.model);
        whisper_->SetLanguage(config_.whisper.language);
        if (FileExists(config_.whisper.vadModel)) {
            whisper_->SetVadModelPath(config_.whisper.vadModel);
        }
    }
    degradeLevel_.store(0);

    // 退避開始時刻からのオフセットで、通常チャンク経路を再利用する。
    const int64_t offsetMs = splitter_->GetConsumedSamples() * 1000 / kTargetRate;

    SpeechChunkSplitter::Config sc;
    sc.sampleRate  = kTargetRate;
    sc.minChunkSec = config_.tuning.chunkMinSec;
    sc.maxChunkSec = config_.tuning.chunkMaxSec;
    SpeechChunkSplitter batchSplitter(sc);

    auto processBatchChunk = [&](const SpeechChunkSplitter::Chunk& c, int64_t silenceRunMs) {
        ProcessChunk(c.samples, offsetMs + c.startMs, c.durationMs, c.hadSpeech, silenceRunMs);
    };

    SpeechChunkSplitter::Chunk c;
    size_t pos = 0;
    const size_t block = kTargetRate; // 1 秒ずつ供給
    while (pos < spill.size()) {
        const size_t n = (std::min)(block, spill.size() - pos);
        if (batchSplitter.Feed(spill.data() + pos, n, c)) {
            processBatchChunk(c, batchSplitter.GetSilenceRunMs());
        }
        pos += n;
    }
    if (batchSplitter.Flush(c)) {
        processBatchChunk(c, batchSplitter.GetSilenceRunMs());
    }
}

bool AudioAnalysisPipeline::CanSummarize() const {
    return summarizer_ != nullptr && summarizer_->IsAvailable() &&
           !running_.load() && !finalizing_.load() && !summarizing_.load();
}

bool AudioAnalysisPipeline::SummarizeExisting(const std::string& jsonlPath) {
    // 録画中・終了処理中・要約中・モデル無効なら実行しない（LLM の同時使用を避ける）。
    if (!CanSummarize()) {
        return false;
    }
    // 前回の要約スレッドを回収してから再代入する。
    if (summarizeThread_.joinable()) {
        summarizeThread_.join();
    }
    summarizing_.store(true);
    summarizeThread_ = std::thread([this, jsonlPath]() {
        // 開いている .jsonl から文字起こしと（合成イベントを含む）イベント要点を復元する。
        TimelineData tl = TimelineJsonlReader::LoadFromFile(jsonlPath);
        std::string transcript;
        for (const auto& e : tl.entries) {
            if (e.type == "speech" && !e.text.empty()) {
                transcript += e.text;
                transcript += "\n";
            }
        }
        const std::string digest = BuildEventDigest(tl);

        if (!transcript.empty()) {
            LOG_INFO("AudioAnalysisPipeline: summarizing '{}'...", jsonlPath);
            const std::string summary = summarizer_->Summarize(transcript, digest);
            if (!summary.empty()) {
                // 追記のみ（既存タイムラインを壊さない）。読み込み側は最後の summary を採用する。
                TimelineJsonlWriter w;
                if (w.Open(jsonlPath)) {
                    w.Write(MakeSummaryEvent(summary));
                    w.Close();
                }
            }
        } else {
            LOG_WARN("AudioAnalysisPipeline: no transcript in '{}'; summary skipped", jsonlPath);
        }
        summarizing_.store(false);
    });
    return true;
}

AudioAnalysisPipeline::Status AudioAnalysisPipeline::GetStatus() const {
    Status s;
    s.transcribeEnabled = transcribeEnabled_;
    s.classifyEnabled   = classifier_ != nullptr;
    s.summarizeEnabled  = summarizer_ != nullptr;
    s.finalizing        = finalizing_.load();
    s.degradeLevel      = degradeLevel_.load();
    s.lastRtf           = lastRtf_.load();
    s.chunksProcessed   = chunksProcessed_.load();
    s.speechEvents      = speechEvents_.load();
    {
        std::lock_guard<std::mutex> l(queueMutex_);
        s.queuedSamples = inputQueue_.size();
    }
    return s;
}

} // namespace LogGuide
