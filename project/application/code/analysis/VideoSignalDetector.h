#pragma once

/// stl
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

/// externals
#include <nlohmann/json.hpp>

namespace LogGuide {

// =============================================================================
// VideoSignalDetector
//
// ダウンスケール済みの輝度フレーム列から、映像の「信号レベル」イベントを検出する。
// 映像の意味（何が映っているか）は一切解釈しない。VLM も使わない。
//
//   - 画面停滞（screen_static_start / screen_static_end）
//       フレーム間の平均絶対差が staticThreshold 以下の状態が staticDurationMs 続く。
//   - 暗転・ロード（screen_blank_start / screen_blank_end）
//       ほぼ単色（黒 or 白）のフレームが blankDurationMs 続く。
//   - 画面遷移（screen_transition）
//       差分量が直近の分布から見て外れ値（z スコア）になったフレーム。
//       固定しきい値ではなく z スコアにするのは、常に画面が動くアクションと
//       ほぼ静止しているノベル/パズルで「遷移」の絶対値が桁違いに異なるため。
//       SignalEventDetector の音量スパイク検出と同じ考え方（Welford + z）。
//   - 画面遷移頻発（screen_thrash）
//       screen_transition が thrashWindowMs 内に thrashCount 件以上集中した。
//
// 状態を跨いで持つため 1 セッション 1 インスタンスで使う。
// 検出したイベントは nlohmann::json として返し、呼び出し側が JSONL へ書く。
//
// 静的なゲームでの screen_static 連発を抑えるため、停滞の解除にはヒステリシスを
// 入れてある（staticExitMs 未満の一瞬の動きでは停滞区間を閉じない）。ノベルの
// テキスト送りのような単発の変化で停滞が細切れになるのを防ぐ。
// 暗転中は停滞ランをリセットする（ロード画面を「詰まり」と誤認させないため）。
// =============================================================================
class VideoSignalDetector {
public:
    struct Config {
        float   staticThreshold      = 0.02f;
        int64_t staticDurationMs     = 30000;
        int64_t staticExitMs         = 1500;
        float   blankLumaThreshold   = 0.05f;
        int64_t blankDurationMs      = 1000;
        float   transitionZ          = 3.0f;
        int64_t transitionDebounceMs = 1000;
        int64_t thrashWindowMs       = 10000;
        int     thrashCount          = 4;
        int64_t thrashCooldownMs     = 15000;
        int     warmupSamples        = 10;
    };

    explicit VideoSignalDetector(const Config& config) : config_(config) {}

    // 1 サンプル分のダウンスケール輝度（各要素 [0,1]）を与える。
    // luma のサイズはセッション中一定であること。検出結果を out へ追加する。
    void ProcessSample(int64_t timeMs, const std::vector<float>& luma,
                       std::vector<nlohmann::json>& out);

    // 録画終了時に呼ぶ。開いたままの停滞/暗転区間を timeMs で閉じる。
    void Flush(int64_t timeMs, std::vector<nlohmann::json>& out);

    // 直近サンプルの正規化差分量（UI 表示・しきい値調整用）。
    double GetLastDiff() const { return lastDiff_; }

private:
    // 停滞区間を閉じる（開いていなければ何もしない）。
    void CloseStatic(int64_t endMs, std::vector<nlohmann::json>& out);

    Config config_;

    std::vector<float> prevLuma_;
    double             lastDiff_ = 0.0;

    // 画面停滞の状態機械。
    bool    inStatic_    = false;
    int64_t staticStartMs_ = 0;
    int64_t stillSinceMs_  = -1; // 動きなしが始まった時刻（停滞判定前）
    int64_t activeSinceMs_ = -1; // 停滞中に動きが戻った時刻（解除判定前）

    // 暗転の状態機械。
    bool        inBlank_          = false;
    int64_t     blankStartMs_     = 0;
    int64_t     blankSinceMs_     = -1; // 単色フレームが始まった時刻（確定前）
    std::string blankKind_;             // "black" / "white"

    // 画面遷移: 差分量のオンライン統計（Welford）。
    int64_t diffCount_    = 0;
    double  diffMean_     = 0.0;
    double  diffM2_       = 0.0;
    int64_t lastTransitionMs_ = -1;

    // 画面遷移頻発: 窓内の遷移時刻。
    std::deque<int64_t> transitionTimes_;
    int64_t             lastThrashMs_ = -1;
};

} // namespace LogGuide
