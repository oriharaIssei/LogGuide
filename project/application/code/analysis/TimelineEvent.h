#pragma once

/// stl
#include <cstdint>
#include <string>
#include <vector>

/// externals
#include <nlohmann/json.hpp>

// =============================================================================
// タイムラインイベント（AI 解析パイプラインの出力単位）。
//
// 1 イベント = JSONL の 1 行。スキーマは AI_ANALYSIS_SPEC.md v1 に準拠する:
//   {"time_ms":42000,"type":"speech","text":"...","tag":"confusion","confidence":0.8}
//   {"time_ms":191000,"type":"silence_start"}
//   {"time_ms":278000,"type":"silence_end","duration_ms":87000}
//   {"time_ms":300000,"type":"volume_spike","label":"laugh_candidate"}
//   {"time_ms":0,"type":"meta","version":1,"source":"session.mp4"}
//   {"time_ms":3514000,"type":"degradation","reason":"gpu_pressure","action":"fallback_medium"}
//
// VIDEO_CORRELATION_SPEC.md v1 で映像シグナルと合成イベントを追加した:
//   {"time_ms":193000,"type":"screen_static_start"}
//   {"time_ms":238000,"type":"screen_static_end","duration_ms":45000}
//   {"time_ms":100000,"type":"screen_blank_start","kind":"black"}
//   {"time_ms":104000,"type":"screen_blank_end","duration_ms":4000}
//   {"time_ms":120000,"type":"screen_transition","diff":0.41}
//   {"time_ms":120000,"type":"screen_thrash","count":5,"window_ms":10000}
//   {"time_ms":193000,"type":"stuck_candidate","sources":[...],"overlap_ms":45000}
//   {"time_ms":121500,"type":"unexpected_reaction","sources":[...],"gap_ms":1500}
//   {"time_ms":191000,"type":"focus_likely","sources":[...],"duration_ms":87000}
//
// フィールドはイベント種別ごとに可変なので、生の nlohmann::json を 1 行として持つ。
// 生成は下の Make... ヘルパを使い、type ごとのスキーマをここに集約する。
// =============================================================================

namespace LogGuide {

// イベント種別。JSONL の "type" 文字列に 1:1 対応する。
enum class TimelineEventType {
    Meta,
    Speech,
    SilenceStart,
    SilenceEnd,
    VolumeSpike,
    SpeechDensity,
    Degradation,
    Summary,
    // --- 映像シグナル（フレーム差分ベース） ---
    ScreenStaticStart,
    ScreenStaticEnd,
    ScreenBlankStart,
    ScreenBlankEnd,
    ScreenTransition,
    ScreenThrash,
    // --- クロスモーダル合成イベント ---
    StuckCandidate,
    UnexpectedReaction,
    FocusLikely,
};

inline const char* ToString(TimelineEventType type) {
    switch (type) {
    case TimelineEventType::Meta:              return "meta";
    case TimelineEventType::Speech:            return "speech";
    case TimelineEventType::SilenceStart:      return "silence_start";
    case TimelineEventType::SilenceEnd:        return "silence_end";
    case TimelineEventType::VolumeSpike:       return "volume_spike";
    case TimelineEventType::SpeechDensity:     return "speech_density";
    case TimelineEventType::Degradation:       return "degradation";
    case TimelineEventType::Summary:           return "summary";
    case TimelineEventType::ScreenStaticStart: return "screen_static_start";
    case TimelineEventType::ScreenStaticEnd:   return "screen_static_end";
    case TimelineEventType::ScreenBlankStart:  return "screen_blank_start";
    case TimelineEventType::ScreenBlankEnd:    return "screen_blank_end";
    case TimelineEventType::ScreenTransition:  return "screen_transition";
    case TimelineEventType::ScreenThrash:      return "screen_thrash";
    case TimelineEventType::StuckCandidate:    return "stuck_candidate";
    case TimelineEventType::UnexpectedReaction:return "unexpected_reaction";
    case TimelineEventType::FocusLikely:       return "focus_likely";
    }
    return "unknown";
}

// 合成イベントが参照する元イベント（type + time_ms の組）。
struct EventSourceRef {
    std::string type;
    int64_t     timeMs = 0;
};

// ---- イベント生成ヘルパ（type ごとのスキーマをここで固定する） --------------

inline nlohmann::json MakeMetaEvent(int version, const std::string& source) {
    return {{"time_ms", 0}, {"type", "meta"}, {"version", version}, {"source", source}};
}

// tag / confidence は分類前だと未確定。tag 空文字なら付与しない。
inline nlohmann::json MakeSpeechEvent(int64_t timeMs, const std::string& text,
                                      const std::string& tag = "", double confidence = 0.0) {
    nlohmann::json e = {{"time_ms", timeMs}, {"type", "speech"}, {"text", text}};
    if (!tag.empty()) {
        e["tag"]        = tag;
        e["confidence"] = confidence;
    }
    return e;
}

inline nlohmann::json MakeSilenceStartEvent(int64_t timeMs) {
    return {{"time_ms", timeMs}, {"type", "silence_start"}};
}

inline nlohmann::json MakeSilenceEndEvent(int64_t timeMs, int64_t durationMs) {
    return {{"time_ms", timeMs}, {"type", "silence_end"}, {"duration_ms", durationMs}};
}

inline nlohmann::json MakeVolumeSpikeEvent(int64_t timeMs, const std::string& label) {
    return {{"time_ms", timeMs}, {"type", "volume_spike"}, {"label", label}};
}

inline nlohmann::json MakeSpeechDensityEvent(int64_t timeMs, int count, int windowMs) {
    return {{"time_ms", timeMs}, {"type", "speech_density"}, {"count", count}, {"window_ms", windowMs}};
}

inline nlohmann::json MakeDegradationEvent(int64_t timeMs, const std::string& reason,
                                           const std::string& action) {
    return {{"time_ms", timeMs}, {"type", "degradation"}, {"reason", reason}, {"action", action}};
}

inline nlohmann::json MakeSummaryEvent(const std::string& text) {
    return {{"time_ms", 0}, {"type", "summary"}, {"text", text}};
}

// ---- 映像シグナル（VideoSignalDetector が生成する） ------------------------

inline nlohmann::json MakeScreenStaticStartEvent(int64_t timeMs) {
    return {{"time_ms", timeMs}, {"type", "screen_static_start"}};
}

inline nlohmann::json MakeScreenStaticEndEvent(int64_t timeMs, int64_t durationMs) {
    return {{"time_ms", timeMs}, {"type", "screen_static_end"}, {"duration_ms", durationMs}};
}

// kind は "black" / "white"。
inline nlohmann::json MakeScreenBlankStartEvent(int64_t timeMs, const std::string& kind) {
    return {{"time_ms", timeMs}, {"type", "screen_blank_start"}, {"kind", kind}};
}

inline nlohmann::json MakeScreenBlankEndEvent(int64_t timeMs, int64_t durationMs) {
    return {{"time_ms", timeMs}, {"type", "screen_blank_end"}, {"duration_ms", durationMs}};
}

// diff は直前サンプルとの正規化差分量 [0,1]。
inline nlohmann::json MakeScreenTransitionEvent(int64_t timeMs, double diff) {
    return {{"time_ms", timeMs}, {"type", "screen_transition"}, {"diff", diff}};
}

inline nlohmann::json MakeScreenThrashEvent(int64_t timeMs, int count, int64_t windowMs) {
    return {{"time_ms", timeMs}, {"type", "screen_thrash"}, {"count", count}, {"window_ms", windowMs}};
}

// ---- クロスモーダル合成（CrossModalCorrelator が生成する） -----------------

inline nlohmann::json MakeSourcesArray(const std::vector<EventSourceRef>& sources) {
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& s : sources) {
        arr.push_back({{"type", s.type}, {"time_ms", s.timeMs}});
    }
    return arr;
}

// 無発話 × 画面停滞。timeMs は重複区間の開始（ここへジャンプすれば詰まりが見える）。
inline nlohmann::json MakeStuckCandidateEvent(int64_t timeMs, const std::vector<EventSourceRef>& sources,
                                              int64_t overlapMs) {
    return {{"time_ms", timeMs}, {"type", "stuck_candidate"},
            {"sources", MakeSourcesArray(sources)}, {"overlap_ms", overlapMs}};
}

// 画面遷移の直後の confusion/surprise 発話。gapMs は遷移から発話までの間隔。
inline nlohmann::json MakeUnexpectedReactionEvent(int64_t timeMs, const std::vector<EventSourceRef>& sources,
                                                  int64_t gapMs) {
    return {{"time_ms", timeMs}, {"type", "unexpected_reaction"},
            {"sources", MakeSourcesArray(sources)}, {"gap_ms", gapMs}};
}

// 無発話だが画面が動いている = 集中状態。参照した無発話イベントはビューアで抑制される。
inline nlohmann::json MakeFocusLikelyEvent(int64_t timeMs, const std::vector<EventSourceRef>& sources,
                                           int64_t durationMs) {
    return {{"time_ms", timeMs}, {"type", "focus_likely"},
            {"sources", MakeSourcesArray(sources)}, {"duration_ms", durationMs}};
}

} // namespace LogGuide
