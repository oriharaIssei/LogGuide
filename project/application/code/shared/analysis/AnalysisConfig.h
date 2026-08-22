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
//   [video_analysis] フレーム差分ベースの映像シグナル検出パラメータ
//   [correlation]   音声×映像のクロスモーダル相関ルールのしきい値
//
// model が空文字列 / 未指定なら、その役割の機能は無効化される（段階的縮退）。
// video_analysis.enabled = false なら従来どおり音声のみで動作する。
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

// 映像シグナル検出（フレーム差分）。CPU のみで完結する軽量処理。
// 単位は ms。閾値はゲームのジャンル差が大きいので、実測して調整する前提。
struct VideoAnalysisTuning {
    bool  enabled              = true;
    int   sampleFps            = 3;     // 差分を測るサンプリングレート
    int   downscaleWidth       = 160;   // 差分計算用のダウンスケール幅（高さはアスペクト比維持）
    float staticThreshold      = 0.02f; // 正規化差分量。これ以下なら「動きなし」
    int   staticDurationMs     = 30000; // 動きなしがこの時間続いたら screen_static_start
    int   staticExitMs         = 1500;  // 停滞の解除に必要な連続活動時間（ヒステリシス）
    float blankLumaThreshold   = 0.05f; // 平均輝度がこの下/上（1-x）かつ一様なら暗転/白飛び
    int   blankDurationMs      = 1000;  // 単色フレームがこの時間続いたら screen_blank_start
    float transitionZ          = 3.0f;  // 差分量の z スコアがこれ以上なら画面遷移
    int   transitionDebounceMs = 1000;  // 画面遷移検出の最小間隔
    int   thrashWindowMs       = 10000; // この窓に
    int   thrashCount          = 4;     // これだけ遷移が集中したら screen_thrash
    int   thrashCooldownMs     = 15000; // screen_thrash の最小間隔
    int   warmupSamples        = 10;    // z スコア判定を始めるまでに貯めるサンプル数
};

// クロスモーダル相関（音声イベント × 映像イベント）。録画終了後に遅延評価する。
struct CorrelationTuning {
    bool enabled             = true;
    int  stuckMinOverlapMs   = 15000; // 無発話と画面停滞の重複がこれ以上で stuck_candidate
    int  reactionWindowMs    = 5000;  // 画面遷移からこの時間内の confusion/surprise で unexpected_reaction
    int  focusMinDurationMs  = 30000; // 無発話がこの長さ以上で focus_likely の候補
    int  focusMinTransitions = 1;     // かつ区間内にこれだけ画面遷移があれば「集中」とみなす
};

struct AnalysisConfig {
    LlmRoleConfig       classify;
    LlmRoleConfig       summarize;
    LlmRoleConfig       report;
    WhisperConfig       whisper;
    AnalysisTuning      tuning;
    VideoAnalysisTuning video;
    CorrelationTuning   correlation;

    // logguide.toml を読み込む。ファイルが無い場合は既定値のまま true を返す
    // （設定ファイル無し = 全 LLM 無効・信号レベルのみ、で動作させる）。
    // outError にパース時の警告を返す（致命的でない限り true）。
    static AnalysisConfig LoadFromFile(const std::string& tomlPath, std::string* outError = nullptr);
};

} // namespace LogGuide
