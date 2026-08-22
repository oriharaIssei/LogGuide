#pragma once

/// stl
#include <memory>
#include <string>

namespace LogGuide {

class LocalLLM;

// =============================================================================
// TranscriptClassifier
//
// 文字起こしチャンクをローカル LLM に渡し、UX 上重要な発話を分類する
// （意味レベル検出）。分類タグ:
//   confusion（迷い）/ frustration（不満）/ delight（喜び）/ surprise（驚き）/ none
//
// - LLM には JSON のみを返すよう指示する。パース失敗時は 1 回リトライし、
//   それでも失敗したら "unclassified" として記録する。
// - プロンプトはシンプルな指示＋少数 shot に留め、0.8B〜9B の幅で動くようにする
//   （特定モデルへの過適合を避ける）。キーワード辞書は使わない。
// =============================================================================
class TranscriptClassifier {
public:
    struct Result {
        std::string tag;        // 上記タグ、または "unclassified"
        double      confidence; // 0.0〜1.0（unclassified 時は 0）
    };

    // llm は分類用モデルをロード済みであること。未ロードなら Classify は
    // 常に unclassified を返す（機能無効時の縮退）。
    explicit TranscriptClassifier(LocalLLM* llm) : llm_(llm) {}

    bool IsAvailable() const;

    // text を分類する。LLM 無効・空テキスト時は unclassified。
    Result Classify(const std::string& text);

private:
    std::string BuildPrompt(const std::string& text) const;
    // LLM 出力から最初の JSON オブジェクトを取り出して tag/confidence を読む。
    // 成功時 true。失敗時 false（呼び出し側がリトライ判断する）。
    bool ParseResponse(const std::string& raw, Result& out) const;

    LocalLLM* llm_ = nullptr;
};

} // namespace LogGuide
