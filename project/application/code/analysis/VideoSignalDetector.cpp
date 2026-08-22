#include "analysis/VideoSignalDetector.h"

#include "analysis/TimelineEvent.h"

/// stl
#include <cmath>

namespace LogGuide {

void VideoSignalDetector::CloseStatic(int64_t endMs, std::vector<nlohmann::json>& out) {
    if (!inStatic_) {
        return;
    }
    const int64_t duration = endMs > staticStartMs_ ? endMs - staticStartMs_ : 0;
    out.push_back(MakeScreenStaticEndEvent(endMs, duration));
    inStatic_      = false;
    activeSinceMs_ = -1;
    stillSinceMs_  = -1;
}

void VideoSignalDetector::ProcessSample(int64_t timeMs, const std::vector<float>& luma,
                                        std::vector<nlohmann::json>& out) {
    if (luma.empty()) {
        return;
    }

    // --- 平均輝度と輝度のばらつき（暗転・白飛び判定用） ---
    double sum = 0.0;
    for (float v : luma) {
        sum += v;
    }
    const double mean = sum / static_cast<double>(luma.size());
    double varSum = 0.0;
    for (float v : luma) {
        const double d = v - mean;
        varSum += d * d;
    }
    const double stddev = std::sqrt(varSum / static_cast<double>(luma.size()));

    const double bt = config_.blankLumaThreshold;
    const bool isDark    = mean <= bt;
    const bool isBright  = mean >= 1.0 - bt;
    const bool isUniform = stddev <= bt;
    const bool blankNow  = isUniform && (isDark || isBright);

    // --- 暗転・ロードの状態機械 ---
    if (blankNow) {
        if (!inBlank_) {
            if (blankSinceMs_ < 0) {
                blankSinceMs_ = timeMs;
                blankKind_    = isDark ? "black" : "white";
            }
            if (timeMs - blankSinceMs_ >= config_.blankDurationMs) {
                blankStartMs_ = blankSinceMs_;
                out.push_back(MakeScreenBlankStartEvent(blankStartMs_, blankKind_));
                inBlank_ = true;
            }
        }
    } else {
        if (inBlank_) {
            const int64_t duration = timeMs > blankStartMs_ ? timeMs - blankStartMs_ : 0;
            out.push_back(MakeScreenBlankEndEvent(timeMs, duration));
            inBlank_ = false;
        }
        blankSinceMs_ = -1;
    }

    // --- 前フレームとの平均絶対差 ---
    if (prevLuma_.size() != luma.size()) {
        prevLuma_ = luma;
        return; // 初回（およびサイズ変更時）は差分を取れない
    }
    double diffSum = 0.0;
    for (size_t i = 0; i < luma.size(); ++i) {
        diffSum += std::fabs(static_cast<double>(luma[i]) - static_cast<double>(prevLuma_[i]));
    }
    const double diff = diffSum / static_cast<double>(luma.size());
    prevLuma_ = luma;
    lastDiff_ = diff;

    // --- 画面停滞 ---
    // 暗転中はロード画面とみなし、停滞ランを進めない（詰まりとの誤認を避ける）。
    // 停滞ラン自体もリセットする。さもないと暗転が明けた瞬間に、暗転の長さぶんの
    // 「動きなし」が溜まっていて screen_static_start が即発火してしまう。
    if (blankNow) {
        CloseStatic(timeMs, out);
        stillSinceMs_ = -1;
    } else {
        const bool isStill = diff <= config_.staticThreshold;
        if (!inStatic_) {
            if (isStill) {
                if (stillSinceMs_ < 0) {
                    stillSinceMs_ = timeMs;
                }
                if (timeMs - stillSinceMs_ >= config_.staticDurationMs) {
                    staticStartMs_ = stillSinceMs_;
                    out.push_back(MakeScreenStaticStartEvent(staticStartMs_));
                    inStatic_      = true;
                    activeSinceMs_ = -1;
                }
            } else {
                stillSinceMs_ = -1;
            }
        } else {
            if (isStill) {
                // 一瞬の動きは無視する（テキスト送り等で停滞区間が細切れになるのを防ぐ）。
                activeSinceMs_ = -1;
            } else {
                if (activeSinceMs_ < 0) {
                    activeSinceMs_ = timeMs;
                }
                if (timeMs - activeSinceMs_ >= config_.staticExitMs) {
                    // 停滞の終了時刻は「動きが戻った瞬間」であって、それを確認した今ではない。
                    CloseStatic(activeSinceMs_, out);
                }
            }
        }
    }

    // --- 画面遷移（差分量の外れ値） ---
    // 統計が十分貯まっていれば、更新前に外れ値判定する
    // （遷移自身が平均を押し上げて自己隠蔽するのを避ける）。
    if (diff > config_.staticThreshold && diffCount_ >= config_.warmupSamples) {
        const double variance = diffM2_ / static_cast<double>(diffCount_);
        const double sd       = std::sqrt(variance);
        if (sd > 1e-6) {
            const double z = (diff - diffMean_) / sd;
            if (z >= config_.transitionZ &&
                (lastTransitionMs_ < 0 || timeMs - lastTransitionMs_ >= config_.transitionDebounceMs)) {
                out.push_back(MakeScreenTransitionEvent(timeMs, diff));
                lastTransitionMs_ = timeMs;
                transitionTimes_.push_back(timeMs);
            }
        }
    }

    // Welford によるオンライン平均/分散更新。
    ++diffCount_;
    const double delta = diff - diffMean_;
    diffMean_ += delta / static_cast<double>(diffCount_);
    diffM2_   += delta * (diff - diffMean_);

    // --- 画面遷移頻発 ---
    while (!transitionTimes_.empty() && timeMs - transitionTimes_.front() > config_.thrashWindowMs) {
        transitionTimes_.pop_front();
    }
    if (static_cast<int>(transitionTimes_.size()) >= config_.thrashCount &&
        (lastThrashMs_ < 0 || timeMs - lastThrashMs_ >= config_.thrashCooldownMs)) {
        out.push_back(MakeScreenThrashEvent(transitionTimes_.front(),
                                            static_cast<int>(transitionTimes_.size()),
                                            config_.thrashWindowMs));
        lastThrashMs_ = timeMs;
        transitionTimes_.clear();
    }
}

void VideoSignalDetector::Flush(int64_t timeMs, std::vector<nlohmann::json>& out) {
    if (inBlank_) {
        const int64_t duration = timeMs > blankStartMs_ ? timeMs - blankStartMs_ : 0;
        out.push_back(MakeScreenBlankEndEvent(timeMs, duration));
        inBlank_ = false;
    }
    CloseStatic(timeMs, out);
}

} // namespace LogGuide
