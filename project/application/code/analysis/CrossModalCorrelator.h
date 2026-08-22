#pragma once

/// stl
#include <string>
#include <vector>

/// externals
#include <nlohmann/json.hpp>

/// module
#include "analysis/AnalysisConfig.h"

namespace LogGuide {

struct TimelineData;

// =============================================================================
// CrossModalCorrelator
//
// 確定した音声イベントと映像イベントを突き合わせ、単独より確度の高い合成イベントを
// 生成する。録画中のリアルタイム判定は不要で、録画終了後に JSONL 全体を読んで
// 一度だけ走らせる（遅延評価）。
//
// v1 のルールは 3 本のみ。ルールエンジン化も外部定義化もしない（YAGNI）:
//
//   stuck_candidate     無発話 と 画面停滞 の重複が stuckMinOverlapMs 以上
//                       → LogGuide の価値提案の中心。ビューアで最も目立たせる。
//   unexpected_reaction 画面遷移の直後 reactionWindowMs 以内に confusion/surprise 発話
//   focus_likely        無発話だが画面が動いている（停滞・暗転と重ならず遷移がある）
//                       → 参照された無発話イベントはビューアで抑制される
//
// 「画面遷移」は screen_transition イベントで表す。フレーム差分の外れ値なので、
// 画面が常に動くアクションでも常時発火はしない（VideoSignalDetector 参照）。
// =============================================================================
class CrossModalCorrelator {
public:
    explicit CrossModalCorrelator(const CorrelationTuning& tuning) : tuning_(tuning) {}

    // 読み込み済みタイムラインから合成イベントを作る（I/O なし）。時刻昇順で返す。
    std::vector<nlohmann::json> Correlate(const TimelineData& timeline) const;

    // jsonlPath を読み、合成イベントを追記する。追記した件数を返す（失敗時 -1）。
    // 既に合成イベントを含むファイルには何もしない（二重追記の防止）。
    int RunOnFile(const std::string& jsonlPath) const;

private:
    CorrelationTuning tuning_;
};

} // namespace LogGuide
