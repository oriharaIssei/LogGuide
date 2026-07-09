#pragma once

/// stl
#include <string>
#include <vector>

namespace LogGuide {

class LocalLLM;

// =============================================================================
// SessionSummarizer
//
// 録画終了後（GPU 独占可能）に、文字起こし全文とタイムラインイベントの要点を
// ローカル LLM へ渡し、セッション要約を生成する（LLM summarize）。
// 要約は summary イベントとして JSONL に追記される。
//
// summarize モデルが未設定なら機能無効（IsAvailable() == false）。
// =============================================================================
class SessionSummarizer {
public:
    explicit SessionSummarizer(LocalLLM* llm) : llm_(llm) {}

    bool IsAvailable() const;

    // fullTranscript: 文字起こし全文。eventDigest: 検出イベントの要点行（任意）。
    // 生成した要約文字列を返す。無効・失敗時は空文字列。
    std::string Summarize(const std::string& fullTranscript,
                          const std::string& eventDigest) const;

private:
    LocalLLM* llm_ = nullptr;
};

} // namespace LogGuide
