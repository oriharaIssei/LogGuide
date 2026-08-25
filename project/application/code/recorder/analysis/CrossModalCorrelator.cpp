#include "analysis/CrossModalCorrelator.h"

/// module
#include "analysis/TimelineEvent.h"
#include "analysis/TimelineJsonlReader.h"
#include "analysis/TimelineJsonlWriter.h"

/// engine
#include "logger/Logger.h"

/// stl
#include <algorithm>

namespace LogGuide {

namespace {

// 開始/終了イベントの対で表される区間。
struct Interval {
    int64_t startMs = 0;
    int64_t endMs   = 0;

    int64_t Duration() const { return endMs > startMs ? endMs - startMs : 0; }
};

// startType / endType の対を区間に畳む。終了が来ないまま終わった区間は fallbackEndMs で閉じる。
std::vector<Interval> CollectIntervals(const TimelineData& timeline, const char* startType,
                                       const char* endType, int64_t fallbackEndMs) {
    std::vector<Interval> intervals;
    bool    open    = false;
    int64_t startMs = 0;

    for (const auto& e : timeline.entries) {
        if (e.type == startType) {
            // 終了なしで次の開始が来た場合は、直前の区間をそこで閉じる（壊れた JSONL への保険）。
            if (open) {
                intervals.push_back({startMs, e.timeMs});
            }
            open    = true;
            startMs = e.timeMs;
        } else if (e.type == endType && open) {
            intervals.push_back({startMs, e.timeMs});
            open = false;
        }
    }
    if (open) {
        intervals.push_back({startMs, (std::max)(startMs, fallbackEndMs)});
    }
    return intervals;
}

std::vector<int64_t> CollectTimes(const TimelineData& timeline, const char* type) {
    std::vector<int64_t> times;
    for (const auto& e : timeline.entries) {
        if (e.type == type) {
            times.push_back(e.timeMs);
        }
    }
    return times;
}

int64_t OverlapMs(const Interval& a, const Interval& b) {
    const int64_t lo = (std::max)(a.startMs, b.startMs);
    const int64_t hi = (std::min)(a.endMs, b.endMs);
    return hi > lo ? hi - lo : 0;
}

bool IsCompositeType(const std::string& type) {
    return type == "stuck_candidate" || type == "unexpected_reaction" || type == "focus_likely";
}

} // namespace

std::vector<nlohmann::json> CrossModalCorrelator::Correlate(const TimelineData& timeline) const {
    std::vector<nlohmann::json> out;
    if (!tuning_.enabled || timeline.entries.empty()) {
        return out;
    }

    // 未完の区間を閉じるための「セッション末尾」。entries は時刻昇順。
    const int64_t lastMs = timeline.entries.back().timeMs;

    const std::vector<Interval> silences = CollectIntervals(timeline, "silence_start", "silence_end", lastMs);
    const std::vector<Interval> statics  = CollectIntervals(timeline, "screen_static_start", "screen_static_end", lastMs);
    const std::vector<Interval> blanks   = CollectIntervals(timeline, "screen_blank_start", "screen_blank_end", lastMs);
    const std::vector<int64_t>  transitions = CollectTimes(timeline, "screen_transition");

    // --- ルール 1: 詰まり候補（無発話 × 画面停滞） ---
    for (const auto& s : silences) {
        for (const auto& t : statics) {
            const int64_t overlap = OverlapMs(s, t);
            if (overlap < tuning_.stuckMinOverlapMs) {
                continue;
            }
            // 重複の開始へジャンプすれば「黙って画面も止まっている」瞬間が見える。
            const int64_t at = (std::max)(s.startMs, t.startMs);
            out.push_back(MakeStuckCandidateEvent(
                at,
                {{"silence_start", s.startMs}, {"screen_static_start", t.startMs}},
                overlap));
        }
    }

    // --- ルール 2: 想定外反応（画面遷移の直後の confusion / surprise 発話） ---
    for (const auto& e : timeline.entries) {
        if (e.type != "speech" || (e.tag != "confusion" && e.tag != "surprise")) {
            continue;
        }
        // 発話の直前 reactionWindowMs 以内で、最も近い画面遷移を採る。
        int64_t best = -1;
        for (int64_t tr : transitions) {
            if (tr <= e.timeMs && e.timeMs - tr <= tuning_.reactionWindowMs && tr > best) {
                best = tr;
            }
        }
        if (best < 0) {
            continue;
        }
        out.push_back(MakeUnexpectedReactionEvent(
            e.timeMs,
            {{"screen_transition", best}, {"speech", e.timeMs}},
            e.timeMs - best));
    }

    // --- ルール 3: 集中状態（無発話だが画面が動いている → 無発話単独の重要度を下げる） ---
    for (const auto& s : silences) {
        if (s.Duration() < tuning_.focusMinDurationMs) {
            continue;
        }
        const bool stalled = std::any_of(statics.begin(), statics.end(),
                                         [&](const Interval& t) { return OverlapMs(s, t) > 0; });
        const bool loading = std::any_of(blanks.begin(), blanks.end(),
                                         [&](const Interval& b) { return OverlapMs(s, b) > 0; });
        if (stalled || loading) {
            continue;
        }
        const auto inside = std::count_if(transitions.begin(), transitions.end(), [&](int64_t tr) {
            return tr >= s.startMs && tr <= s.endMs;
        });
        if (static_cast<int>(inside) < tuning_.focusMinTransitions) {
            continue;
        }
        out.push_back(MakeFocusLikelyEvent(
            s.startMs,
            {{"silence_start", s.startMs}, {"silence_end", s.endMs}},
            s.Duration()));
    }

    std::sort(out.begin(), out.end(), [](const nlohmann::json& a, const nlohmann::json& b) {
        return a.value("time_ms", int64_t{0}) < b.value("time_ms", int64_t{0});
    });
    return out;
}

int CrossModalCorrelator::RunOnFile(const std::string& jsonlPath) const {
    if (!tuning_.enabled) {
        return 0;
    }

    const TimelineData timeline = TimelineJsonlReader::LoadFromFile(jsonlPath);
    if (timeline.Empty()) {
        return 0;
    }
    // 同じファイルに二度追記しない（相関は 1 セッション 1 回で完結する）。
    const bool alreadyCorrelated = std::any_of(
        timeline.entries.begin(), timeline.entries.end(),
        [](const TimelineEntry& e) { return IsCompositeType(e.type); });
    if (alreadyCorrelated) {
        LOG_INFO("CrossModalCorrelator: '{}' already correlated; skipped", jsonlPath);
        return 0;
    }

    const std::vector<nlohmann::json> events = Correlate(timeline);
    if (events.empty()) {
        LOG_INFO("CrossModalCorrelator: no composite events for '{}'", jsonlPath);
        return 0;
    }

    TimelineJsonlWriter writer;
    if (!writer.Open(jsonlPath)) {
        LOG_ERROR("CrossModalCorrelator: failed to open '{}' for append", jsonlPath);
        return -1;
    }
    for (const auto& e : events) {
        writer.Write(e);
    }
    writer.Close();

    LOG_INFO("CrossModalCorrelator: wrote {} composite event(s) to '{}'", events.size(), jsonlPath);
    return static_cast<int>(events.size());
}

} // namespace LogGuide
