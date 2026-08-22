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
        // 合成イベント（音声×映像）は単独シグナルより確度が高い。単に「無発話が 3 回」と
        // 数えるのではなく、詰まり候補の区間をタイムスタンプ付きで報告させる。
        user +=
            "[検出イベント] は各行が [分:秒] で始まります。読み方:\n"
            "  詰まり候補   … 無言のまま画面も止まっていた区間。テスターが詰まっていた可能性が高い。\n"
            "  想定外の反応 … 画面が切り替わった直後に戸惑い/驚きの発話があった箇所。\n"
            "  集中していた可能性 … 無言だが操作は続いていた区間。問題ではないので詰まりとして数えないこと。\n"
            "  無発話       … 上記のどれにも当てはまらない沈黙。\n"
            "詰まり候補と想定外の反応は、件数だけでなく必ず各タイムスタンプを添えて報告すること。\n\n";
        user += "[検出イベント]\n";
        user += eventDigest;
        user += "\n\n";
    }
    user += "[文字起こし]\n";
    user += fullTranscript;

    return llm_->GenerateChat(system, user);
}

} // namespace LogGuide
