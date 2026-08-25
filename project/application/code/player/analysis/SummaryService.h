#pragma once

/// stl
#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <vector>

/// module
#include "analysis/AnalysisConfig.h"

namespace LogGuide {
class LocalLLM;
class SessionSummarizer;

// =============================================================================
// SummaryService
//
// 再生アプリの「要約を生成」ボタンのためだけの、軽量な要約サービス。
// 録画側の AudioAnalysisPipeline は whisper.cpp（文字起こし）を必要とするが、
// こちらは既に .jsonl に書き出し済みの speech イベントを読むだけなので、
// llama.cpp（LocalLLM / SessionSummarizer）だけで完結する。whisper には触れない。
//
// summarize モデルが未設定・ロード失敗なら CanSummarize() が false のままとなり、
// UI 側は要約ボタンを無効表示するだけでアプリ自体は続行できる。
// =============================================================================
class SummaryService {
public:
    SummaryService();
    ~SummaryService();
    SummaryService(const SummaryService&)            = delete;
    SummaryService& operator=(const SummaryService&) = delete;

    // logguide.toml を読み込み、summarize モデルをロードする（重い）。
    // 戻り値は要約機能が使えるか。false でも warnings に理由が積まれるだけで、
    // 呼び出し側（アプリ）は続行してよい。
    bool Initialize(std::vector<std::string>* warnings = nullptr);
    // 走っているスレッドを join してから LLM を解放する。
    void Finalize();

    bool CanSummarize() const; // summarize モデルが有効で、今すぐ実行できるか
    bool IsSummarizing() const;

    // 既存の .jsonl（ビューアで開いているタイムライン）を読み、その文字起こしから
    // セッション要約を生成して summary イベントを追記する。別スレッドで走り UI をブロックしない。
    // summarize モデル無効・要約中・文字起こし空なら false（もしくは何もせず終わる）。
    bool SummarizeExisting(const std::string& jsonlPath);

    const std::vector<std::string>& Warnings() const;

private:
    AnalysisConfig config_;

    std::unique_ptr<LocalLLM>          llm_;
    std::unique_ptr<SessionSummarizer> summarizer_;

    std::thread       summarizeThread_;
    std::atomic<bool> summarizing_{false};

    std::vector<std::string> warnings_; // Initialize で積んだ警告
};
} // namespace LogGuide
