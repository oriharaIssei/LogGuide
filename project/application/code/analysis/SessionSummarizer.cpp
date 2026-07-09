#include "analysis/SessionSummarizer.h"

#include "analysis/LocalLLM.h"

/// stl
#include <string>

namespace LogGuide {

bool SessionSummarizer::IsAvailable() const {
    return llm_ != nullptr && llm_->IsModelLoaded();
}

std::string SessionSummarizer::Summarize(const std::string& fullTranscript,
                                         const std::string& eventDigest) const {
    if (!IsAvailable() || fullTranscript.empty()) {
        return {};
    }

    const std::string system =
        "あなたは UX リサーチャーの補佐です。ユーザーテストの記録を要約します。";

    std::string user =
        "以下はユーザーテストセッションの文字起こしと、自動検出されたイベントです。\n"
        "テスターが「どこで迷ったか・不満を感じたか・喜んだか」を中心に、\n"
        "箇条書き 3〜6 点で日本語で要約してください。憶測は避け、記録にある事実に基づくこと。\n\n";

    if (!eventDigest.empty()) {
        user += "[検出イベント]\n";
        user += eventDigest;
        user += "\n\n";
    }
    user += "[文字起こし]\n";
    user += fullTranscript;

    return llm_->GenerateChat(system, user);
}

} // namespace LogGuide
