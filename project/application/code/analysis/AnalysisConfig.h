#pragma once

/// stl
#include <cstdint>
#include <string>

namespace LogGuide {

// =============================================================================
// AnalysisConfig
//
// logguide.toml から読み込む AI 解析パイプラインの設定。
// 役割ごとにモデルをユーザーが差し替えられるようにし、ハードコードを避ける。
//
//   [llm.classify]  意味レベル検出（発話分類）に使うモデル
//   [llm.summarize] 録画終了後のセッション要約に使うモデル
//   [llm.report]    レポート生成（将来）。既定は空 = 無効。
//   [whisper]       文字起こしモデルと VAD モデル
//   [analysis]      チャンク長・無発話しきい値など信号処理パラメータ
//
// model が空文字列 / 未指定なら、その役割の機能は無効化される（段階的縮退）。
// =============================================================================

struct LlmRoleConfig {
    std::string model;          // gguf パス。空 = この役割を無効化。
    int         maxTokens = 256;
    int         gpuLayers = 99; // -ngl 相当。0 で CPU 実行。
    int         contextSize = 4096;
};

struct WhisperConfig {
    std::string model;          // large モデル（既定）
    std::string modelMedium;    // GPU 逼迫時のフォールバック（空なら縮退で large 継続）
    std::string vadModel;       // Silero VAD モデル
    std::string language = "ja";
};

struct AnalysisTuning {
    bool  enabled              = true; // 解析パイプライン全体の ON/OFF
    int   chunkMinSec          = 10;   // VAD チャンクの最小長
    int   chunkMaxSec          = 30;   // VAD チャンクの最大長（強制分割）
    int   silenceThresholdSec  = 60;   // 無発話イベントとみなす下限
    float vadThreshold         = 0.5f; // Silero VAD しきい値
    float volumeSpikeZ         = 3.0f; // 音量スパイク検出の z スコアしきい値
    int   densityWindowSec     = 20;   // 発話密度を測る窓
    int   densityBurstCount    = 5;    // 窓内この件数以上で density イベント
    int   chunkSleepMs         = 200;  // チャンク処理間スリープ（GPU 優先度を下げる）
    float rtfFallbackThreshold = 1.0f; // 実測 RTF がこれを超え続けたら medium へ縮退
};

struct AnalysisConfig {
    LlmRoleConfig  classify;
    LlmRoleConfig  summarize;
    LlmRoleConfig  report;
    WhisperConfig  whisper;
    AnalysisTuning tuning;

    // logguide.toml を読み込む。ファイルが無い場合は既定値のまま true を返す
    // （設定ファイル無し = 全 LLM 無効・信号レベルのみ、で動作させる）。
    // outError にパース時の警告を返す（致命的でない限り true）。
    static AnalysisConfig LoadFromFile(const std::string& tomlPath, std::string* outError = nullptr);
};

} // namespace LogGuide
