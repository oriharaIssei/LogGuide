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
        e.confidence = j.value("confidence", 0.0);
        e.durationMs = j.value("duration_ms", int64_t{0});
        data.entries.push_back(std::move(e));
    }

    std::stable_sort(data.entries.begin(), data.entries.end(),
                     [](const TimelineEntry& a, const TimelineEntry& b) {
                         return a.timeMs < b.timeMs;
                     });
    return data;
}

std::string TimelineJsonlReader::SidecarPathFor(const std::string& mediaPath) {
    fs::path p(mediaPath);
    p.replace_extension(".jsonl");
    return p.string();
}

} // namespace LogGuide
