#pragma once

/// stl
#include <cstdint>
#include <string>
#include <vector>

namespace LogGuide {

// 合成イベントが参照する元イベント（type + time_ms）。クリックでジャンプできる。
struct TimelineSourceRef {
    std::string type;
    int64_t     timeMs = 0;
};

// ビューア向けに整形したタイムラインイベント 1 件。
struct TimelineEntry {
    int64_t     timeMs = 0;
    std::string type;        // "speech" / "silence_start" / ... / "meta" / "summary"
    std::string text;        // speech / summary の本文
    std::string tag;         // speech の分類タグ（confusion 等）。無ければ空。
    std::string label;       // volume_spike のラベル等
    double      confidence = 0.0;
    int64_t     durationMs = 0; // silence_end / screen_static_end の長さ等
    int64_t     overlapMs  = 0; // stuck_candidate の重複時間
    int64_t     gapMs      = 0; // unexpected_reaction の遷移→発話の間隔
    int         count      = 0; // speech_density / screen_thrash の件数

    // 合成イベント（stuck_candidate 等）が参照する元イベント。
    std::vector<TimelineSourceRef> sources;

    // focus_likely に参照された無発話イベントに立つ。ビューアで既定非表示。
    bool suppressed = false;
};

// 読み込んだタイムライン全体。
struct TimelineData {
    std::string                source;  // meta の source（動画名）
    int                        version = 0;
    std::vector<TimelineEntry> entries; // timeMs 昇順にソート済み
    std::string                summary; // summary イベントがあれば本文

    bool Empty() const { return entries.empty(); }
};

// =============================================================================
// TimelineJsonlReader
//
// JSON Lines 形式のタイムラインを読み込む。破損に強く、途中でパースできない行が
// あってもそこで打ち切って、それまでの有効行を返す（解析中クラッシュで最終行が
// 欠けても直前まで無傷で読める、という書き込み側の保証と対になる）。
//
// JSONL は追記専用なので既存行は書き換えられない。focus_likely が参照する無発話
// イベントの suppressed フラグは、読み込み後にここで解決して立てる。
// =============================================================================
class TimelineJsonlReader {
public:
    // path を読み込む。存在しない/空でも空の TimelineData を返す（例外は投げない）。
    static TimelineData LoadFromFile(const std::string& path);

    // 動画パス（例 screen.mp4）に対応する .jsonl パスを返す（拡張子を .jsonl に置換）。
    static std::string SidecarPathFor(const std::string& mediaPath);
};

} // namespace LogGuide
