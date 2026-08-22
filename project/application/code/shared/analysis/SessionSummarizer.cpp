#include "analysis/SessionSummarizer.h"

#include "analysis/LocalLLM.h"

/// stl
#include <cstdint>
#include <cstdio>
#include <string>

namespace LogGuide {

namespace {

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

} // namespace

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

// タイムライン全体から、要約 LLM に渡すイベント要点を組み立てる。
// 合成イベント（stuck_candidate 等）を優先して含めることで、「無発話が 3 回」ではなく
// 「詰まっていた可能性のある区間が 3 箇所（各タイムスタンプ）」と報告させる。
std::string SessionSummarizer::BuildEventDigest(const TimelineData& timeline) {
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

} // namespace LogGuide
