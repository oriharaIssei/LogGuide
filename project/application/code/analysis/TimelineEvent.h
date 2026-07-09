#pragma once

/// stl
#include <cstdint>
#include <string>

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
};

inline const char* ToString(TimelineEventType type) {
    switch (type) {
    case TimelineEventType::Meta:         return "meta";
    case TimelineEventType::Speech:       return "speech";
    case TimelineEventType::SilenceStart: return "silence_start";
    case TimelineEventType::SilenceEnd:   return "silence_end";
    case TimelineEventType::VolumeSpike:  return "volume_spike";
    case TimelineEventType::SpeechDensity:return "speech_density";
    case TimelineEventType::Degradation:  return "degradation";
    case TimelineEventType::Summary:      return "summary";
    }
    return "unknown";
}

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

} // namespace LogGuide
