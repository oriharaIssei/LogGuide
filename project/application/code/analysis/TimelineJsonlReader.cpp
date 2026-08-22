#include "analysis/TimelineJsonlReader.h"

/// externals
#include <nlohmann/json.hpp>

/// stl
#include <algorithm>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace LogGuide {

TimelineData TimelineJsonlReader::LoadFromFile(const std::string& path) {
    TimelineData data;

    std::ifstream file(path);
    if (!file) {
        return data;
    }

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty()) {
            continue;
        }
        nlohmann::json j = nlohmann::json::parse(line, nullptr, false);
        if (j.is_discarded()) {
            // 破損行（クラッシュで途中まで書かれた最終行など）に達したら打ち切る。
            break;
        }

        const std::string type = j.value("type", "");
        if (type == "meta") {
            data.source  = j.value("source", "");
            data.version = j.value("version", 0);
            continue;
        }
        if (type == "summary") {
            data.summary = j.value("text", "");
            // summary もエントリとして残しておく（リスト表示用）。
        }

        TimelineEntry e;
        e.timeMs     = j.value("time_ms", int64_t{0});
        e.type       = type;
        e.text       = j.value("text", "");
        e.tag        = j.value("tag", "");
        e.label      = j.value("label", "");
        if (e.label.empty()) {
            e.label = j.value("kind", ""); // screen_blank_start の "black"/"white"
        }
        e.confidence = j.value("confidence", 0.0);
        e.durationMs = j.value("duration_ms", int64_t{0});
        e.overlapMs  = j.value("overlap_ms", int64_t{0});
        e.gapMs      = j.value("gap_ms", int64_t{0});
        e.count      = j.value("count", 0);

        if (auto it = j.find("sources"); it != j.end() && it->is_array()) {
            for (const auto& s : *it) {
                TimelineSourceRef ref;
                ref.type   = s.value("type", "");
                ref.timeMs = s.value("time_ms", int64_t{0});
                e.sources.push_back(std::move(ref));
            }
        }
        data.entries.push_back(std::move(e));
    }

    std::stable_sort(data.entries.begin(), data.entries.end(),
                     [](const TimelineEntry& a, const TimelineEntry& b) {
                         return a.timeMs < b.timeMs;
                     });

    // focus_likely が指す無発話イベントを抑制する。JSONL は追記専用で既存行を
    // 書き換えられないため、参照の解決は読み込み側の責務になる。
    for (const auto& fe : data.entries) {
        if (fe.type != "focus_likely") {
            continue;
        }
        for (const auto& ref : fe.sources) {
            for (auto& target : data.entries) {
                if (target.type == ref.type && target.timeMs == ref.timeMs) {
                    target.suppressed = true;
                }
            }
        }
    }
    return data;
}

std::string TimelineJsonlReader::SidecarPathFor(const std::string& mediaPath) {
    fs::path p(mediaPath);
    p.replace_extension(".jsonl");
    return p.string();
}

} // namespace LogGuide
