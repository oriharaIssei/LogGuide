#include "analysis/AudioAnalysisPipeline.h"

/// module
#include "analysis/LocalLLM.h"
#include "analysis/SessionSummarizer.h"
#include "analysis/SignalEventDetector.h"
#include "analysis/SpeechChunkSplitter.h"
#include "analysis/TimelineEvent.h"
#include "analysis/TimelineJsonlWriter.h"
#include "analysis/TranscriptClassifier.h"
#include "analysis/WhisperTranscriber.h"

/// engine
#include "logger/Logger.h"

/// stl
#include <algorithm>
#include <chrono>
#include <cmath>
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

} // namespace

AudioAnalysisPipeline::AudioAnalysisPipeline()  = default;

AudioAnalysisPipeline::~AudioAnalysisPipeline() {
    Stop();
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

bool AudioAnalysisPipeline::Start(const std::string& jsonlPath, const std::string& sourceVideoName) {
    if (running_.load()) {
        return false;
    }
    sourceVideoName_ = sourceVideoName;

    writer_ = std::make_unique<TimelineJsonlWriter>();
    if (!writer_->Open(jsonlPath)) {
        LOG_ERROR("AudioAnalysisPipeline: failed to open jsonl: {}", jsonlPath);
        writer_.reset();
        return false;
    }
    writer_->Write(MakeMetaEvent(1, sourceVideoName));

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

    LOG_INFO("AudioAnalysisPipeline: started -> {}", jsonlPath);
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
    if (!running_.load()) {
        return;
    }
    stopRequested_.store(true);
    queueCv_.notify_all();
    if (worker_.joinable()) {
        worker_.join();
    }

    // 第 3 段縮退で退避した音声があれば、GPU が空いた今バッチ解析する。
    RunBatchAnalysis();

    // セッション要約（有効時）。
    RunSummarize();

    if (writer_) {
        writer_->Close();
        writer_.reset();
    }
    running_.store(false);
    LOG_INFO("AudioAnalysisPipeline: stopped (chunks={}, speech={})",
             chunksProcessed_.load(), speechEvents_.load());
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

void AudioAnalysisPipeline::RunSummarize() {
    if (!summarizer_ || !summarizer_->IsAvailable()) {
        return;
    }
    std::string transcript;
    std::string digest;
    {
        std::lock_guard<std::mutex> l(transcriptMutex_);
        transcript = fullTranscript_;
        for (const auto& d : eventDigest_) {
            digest += d;
            digest += "\n";
        }
    }
    if (transcript.empty()) {
        return;
    }

    LOG_INFO("AudioAnalysisPipeline: summarizing session...");
    const std::string summary = summarizer_->Summarize(transcript, digest);
    if (!summary.empty() && writer_) {
        writer_->Write(MakeSummaryEvent(summary));
    }
}

AudioAnalysisPipeline::Status AudioAnalysisPipeline::GetStatus() const {
    Status s;
    s.transcribeEnabled = transcribeEnabled_;
    s.classifyEnabled   = classifier_ != nullptr;
    s.summarizeEnabled  = summarizer_ != nullptr;
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
