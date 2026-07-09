#pragma once

/// stl
#include <cstdint>
#include <string>
#include <vector>

namespace LogGuide {

// ビューア向けに整形したタイムラインイベント 1 件。
struct TimelineEntry {
    int64_t     timeMs = 0;
    std::string type;        // "speech" / "silence_start" / ... / "meta" / "summary"
    std::string text;        // speech / summary の本文
    std::string tag;         // speech の分類タグ（confusion 等）。無ければ空。
    std::string label;       // volume_spike のラベル等
    double      confidence = 0.0;
    int64_t     durationMs = 0; // silence_end の長さ等
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
// =============================================================================
class TimelineJsonlReader {
public:
    // path を読み込む。存在しない/空でも空の TimelineData を返す（例外は投げない）。
    static TimelineData LoadFromFile(const std::string& path);

    // 動画パス（例 screen.mp4）に対応する .jsonl パスを返す（拡張子を .jsonl に置換）。
    static std::string SidecarPathFor(const std::string& mediaPath);
};

} // namespace LogGuide
