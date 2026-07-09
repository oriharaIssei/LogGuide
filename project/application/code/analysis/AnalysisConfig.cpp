#include "analysis/AnalysisConfig.h"

/// stl
#include <cctype>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>

namespace LogGuide {

namespace {

// 前後の空白を落とす。
std::string Trim(const std::string& s) {
    size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) {
        return {};
    }
    size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

// 値がクオートされていればクオートを剥がす。
std::string Unquote(const std::string& s) {
    if (s.size() >= 2 && (s.front() == '"' || s.front() == '\'') && s.back() == s.front()) {
        return s.substr(1, s.size() - 2);
    }
    return s;
}

// TOML サブセットのパース結果。キーは "section.key" 形式で平坦化して持つ。
// 対応構文: [section] / [a.b]、 key = value、# コメント、文字列/数値/真偽。
// （配列・インラインテーブル・複数行文字列など TOML 全機能は不要なので未対応）
struct FlatToml {
    std::unordered_map<std::string, std::string> values;

    const std::string* Find(const std::string& key) const {
        auto it = values.find(key);
        return it != values.end() ? &it->second : nullptr;
    }
    std::string Str(const std::string& key, const std::string& def) const {
        const std::string* v = Find(key);
        return v ? *v : def;
    }
    int Int(const std::string& key, int def) const {
        const std::string* v = Find(key);
        if (!v) return def;
        try { return std::stoi(*v); } catch (...) { return def; }
    }
    float Float(const std::string& key, float def) const {
        const std::string* v = Find(key);
        if (!v) return def;
        try { return std::stof(*v); } catch (...) { return def; }
    }
    bool Bool(const std::string& key, bool def) const {
        const std::string* v = Find(key);
        if (!v) return def;
        return *v == "true" || *v == "1";
    }
};

FlatToml ParseToml(std::istream& in) {
    FlatToml out;
    std::string section;
    std::string line;
    while (std::getline(in, line)) {
        // 行コメント（クオート内の # は考慮しない簡易実装。設定ファイル用途では十分）。
        size_t hash = line.find('#');
        if (hash != std::string::npos) {
            line = line.substr(0, hash);
        }
        std::string t = Trim(line);
        if (t.empty()) {
            continue;
        }
        if (t.front() == '[' && t.back() == ']') {
            section = Trim(t.substr(1, t.size() - 2));
            continue;
        }
        size_t eq = t.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        std::string key = Trim(t.substr(0, eq));
        std::string val = Unquote(Trim(t.substr(eq + 1)));
        std::string full = section.empty() ? key : section + "." + key;
        out.values[full] = val;
    }
    return out;
}

void LoadRole(const FlatToml& toml, const std::string& role, LlmRoleConfig& cfg, int defMaxTokens) {
    cfg.model       = toml.Str("llm." + role + ".model", "");
    cfg.maxTokens   = toml.Int("llm." + role + ".max_tokens", defMaxTokens);
    cfg.gpuLayers   = toml.Int("llm." + role + ".gpu_layers", 99);
    cfg.contextSize = toml.Int("llm." + role + ".context_size", 4096);
}

} // namespace

AnalysisConfig AnalysisConfig::LoadFromFile(const std::string& tomlPath, std::string* outError) {
    AnalysisConfig cfg;

    std::ifstream file(tomlPath);
    if (!file) {
        if (outError) {
            *outError = "logguide.toml not found (" + tomlPath +
                        "); running with signal-level detection only";
        }
        return cfg; // 既定値: 全 LLM 無効、解析有効
    }

    FlatToml toml = ParseToml(file);

    LoadRole(toml, "classify", cfg.classify, 128);
    LoadRole(toml, "summarize", cfg.summarize, 2048);
    LoadRole(toml, "report", cfg.report, 2048);

    cfg.whisper.model       = toml.Str("whisper.model", "");
    cfg.whisper.modelMedium = toml.Str("whisper.model_medium", "");
    cfg.whisper.vadModel    = toml.Str("whisper.vad_model", "");
    cfg.whisper.language    = toml.Str("whisper.language", "ja");

    cfg.tuning.enabled              = toml.Bool("analysis.enabled", true);
    cfg.tuning.chunkMinSec          = toml.Int("analysis.chunk_min_sec", 10);
    cfg.tuning.chunkMaxSec          = toml.Int("analysis.chunk_max_sec", 30);
    cfg.tuning.silenceThresholdSec  = toml.Int("analysis.silence_threshold_sec", 60);
    cfg.tuning.vadThreshold         = toml.Float("analysis.vad_threshold", 0.5f);
    cfg.tuning.volumeSpikeZ         = toml.Float("analysis.volume_spike_z", 3.0f);
    cfg.tuning.densityWindowSec     = toml.Int("analysis.density_window_sec", 20);
    cfg.tuning.densityBurstCount    = toml.Int("analysis.density_burst_count", 5);
    cfg.tuning.chunkSleepMs         = toml.Int("analysis.chunk_sleep_ms", 200);
    cfg.tuning.rtfFallbackThreshold = toml.Float("analysis.rtf_fallback_threshold", 1.0f);

    return cfg;
}

} // namespace LogGuide
