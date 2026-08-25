#include "analysis/SummaryService.h"

/// module
#include "analysis/LocalLLM.h"
#include "analysis/SessionSummarizer.h"
#include "analysis/TimelineEvent.h"
#include "analysis/TimelineJsonlReader.h"
#include "analysis/TimelineJsonlWriter.h"

/// engine
#include "logger/Logger.h"

/// stl
#include <filesystem>

namespace fs = std::filesystem;

namespace LogGuide {

namespace {

bool FileExists(const std::string& path) {
    if (path.empty()) {
        return false;
    }
    std::error_code ec;
    return fs::exists(path, ec);
}

} // namespace

SummaryService::SummaryService()  = default;

SummaryService::~SummaryService() {
    // 破棄前に、走っている要約処理を必ず完了させる（スレッドが this を参照するため）。
    Finalize();
}

bool SummaryService::Initialize(std::vector<std::string>* warnings) {
    config_ = AnalysisConfig::LoadFromFile("logguide.toml", nullptr);

    auto warn = [&](const std::string& msg) {
        LOG_WARN("SummaryService: {}", msg);
        warnings_.push_back(msg);
        if (warnings) {
            warnings->push_back(msg);
        }
    };

    // 録画側とは異なり、classify 用 LLM を再利用する分岐は不要（再生アプリに classify は無い）。
    if (config_.summarize.model.empty()) {
        warn("summarize model not configured; summarize disabled");
        return false;
    }
    if (!FileExists(config_.summarize.model)) {
        warn("summarize model file not found: " + config_.summarize.model + "; summarize disabled");
        return false;
    }

    llm_ = std::make_unique<LocalLLM>();
    if (!llm_->LoadModel(config_.summarize.model, config_.summarize.gpuLayers, config_.summarize.contextSize)) {
        warn("failed to load summarize model: " + config_.summarize.model + "; summarize disabled");
        llm_.reset();
        return false;
    }
    llm_->SetMaxTokens(config_.summarize.maxTokens);
    summarizer_ = std::make_unique<SessionSummarizer>(llm_.get());

    LOG_INFO("SummaryService: initialized (summarize={})", summarizer_->IsAvailable());
    return true;
}

void SummaryService::Finalize() {
    if (summarizeThread_.joinable()) {
        summarizeThread_.join();
    }
    summarizer_.reset();
    llm_.reset();
}

bool SummaryService::CanSummarize() const {
    return summarizer_ != nullptr && summarizer_->IsAvailable() && !summarizing_.load();
}

bool SummaryService::IsSummarizing() const {
    return summarizing_.load();
}

bool SummaryService::SummarizeExisting(const std::string& jsonlPath) {
    // 要約中・モデル無効なら実行しない（LLM の同時使用を避ける）。
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
        const std::string digest = SessionSummarizer::BuildEventDigest(tl);

        if (!transcript.empty()) {
            LOG_INFO("SummaryService: summarizing '{}'...", jsonlPath);
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
            LOG_WARN("SummaryService: no transcript in '{}'; summary skipped", jsonlPath);
        }
        summarizing_.store(false);
    });
    return true;
}

const std::vector<std::string>& SummaryService::Warnings() const {
    return warnings_;
}

} // namespace LogGuide
