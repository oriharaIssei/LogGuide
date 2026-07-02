#include "recording/SessionManifest.h"

/// stl
#include <fstream>

/// externals
#include <nlohmann/json.hpp>

namespace LogGuide {

namespace {

// session.json のスキーマバージョン。フォーマット変更時にインクリメントする。
constexpr int kSchemaVersion = 1;

nlohmann::json TrackToJson(const TrackInfo& t) {
    nlohmann::json j;
    j["kind"]  = ToString(t.kind);
    j["label"] = t.label;
    j["file"]  = t.file;

    j["video"] = {
        {"device", t.videoDeviceName},
        {"width", t.width},
        {"height", t.height},
        {"fps", t.fps},
        {"bitrate", t.videoBitrate},
    };

    j["audio"] = {
        {"enabled", t.hasAudio},
        {"device", t.audioDeviceName},
        {"bitrate", t.audioBitrate},
    };
    return j;
}

TrackKind ParseKind(const std::string& s) {
    if (s == ToString(TrackKind::ScreenSystemAudio)) {
        return TrackKind::ScreenSystemAudio;
    }
    return TrackKind::CameraMic;
}

} // namespace

bool WriteSessionManifest(const SessionInfo& session, const std::string& jsonPath, std::string* err) {
    nlohmann::json root;
    root["schemaVersion"]   = kSchemaVersion;
    root["app"]             = "LogGuide";
    root["sessionId"]       = session.sessionId;
    root["startedAt"]       = session.startedAtIso;
    root["endedAt"]         = session.endedAtIso;
    root["durationSeconds"] = session.durationSeconds;

    nlohmann::json tracks = nlohmann::json::array();
    for (const auto& t : session.tracks) {
        tracks.push_back(TrackToJson(t));
    }
    root["tracks"] = std::move(tracks);

    std::ofstream ofs(jsonPath, std::ios::binary | std::ios::trunc);
    if (!ofs) {
        if (err) {
            *err = "failed to open manifest for write: " + jsonPath;
        }
        return false;
    }
    ofs << root.dump(4);
    if (!ofs) {
        if (err) {
            *err = "failed to write manifest: " + jsonPath;
        }
        return false;
    }
    return true;
}

bool ReadSessionManifest(const std::string& jsonPath, SessionInfo& outSession, std::string* err) {
    std::ifstream ifs(jsonPath, std::ios::binary);
    if (!ifs) {
        if (err) {
            *err = "failed to open manifest for read: " + jsonPath;
        }
        return false;
    }

    nlohmann::json root;
    try {
        ifs >> root;
    } catch (const std::exception& e) {
        if (err) {
            *err = std::string("failed to parse manifest: ") + e.what();
        }
        return false;
    }

    outSession = SessionInfo{};
    outSession.sessionId       = root.value("sessionId", std::string{});
    outSession.startedAtIso    = root.value("startedAt", std::string{});
    outSession.endedAtIso      = root.value("endedAt", std::string{});
    outSession.durationSeconds = root.value("durationSeconds", 0.0);

    if (root.contains("tracks") && root["tracks"].is_array()) {
        for (const auto& jt : root["tracks"]) {
            TrackInfo t;
            t.kind  = ParseKind(jt.value("kind", std::string{}));
            t.label = jt.value("label", std::string{});
            t.file  = jt.value("file", std::string{});
            if (jt.contains("video")) {
                const auto& v      = jt["video"];
                t.videoDeviceName  = v.value("device", std::string{});
                t.width            = v.value("width", 0u);
                t.height           = v.value("height", 0u);
                t.fps              = v.value("fps", 30u);
                t.videoBitrate     = v.value("bitrate", 0u);
            }
            if (jt.contains("audio")) {
                const auto& a     = jt["audio"];
                t.hasAudio        = a.value("enabled", false);
                t.audioDeviceName = a.value("device", std::string{});
                t.audioBitrate    = a.value("bitrate", 0u);
            }
            outSession.tracks.push_back(std::move(t));
        }
    }
    return true;
}

} // namespace LogGuide
