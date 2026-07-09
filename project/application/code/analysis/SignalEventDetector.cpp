#include "analysis/SignalEventDetector.h"

#include "analysis/TimelineEvent.h"

/// stl
#include <cmath>

namespace LogGuide {

void SignalEventDetector::ProcessChunkAudio(int64_t chunkStartMs, const std::vector<float>& samples,
                                            std::vector<nlohmann::json>& out) {
    const size_t frame = config_.frameSamples;
    if (frame == 0) {
        return;
    }
    const int64_t frameMs = static_cast<int64_t>(frame) * 1000 / static_cast<int64_t>(config_.sampleRate);

    for (size_t pos = 0, idx = 0; pos + frame <= samples.size(); pos += frame, ++idx) {
        float sumSq = 0.0f;
        for (size_t i = 0; i < frame; ++i) {
            const float s = samples[pos + i];
            sumSq += s * s;
        }
        const double rms = std::sqrt(sumSq / static_cast<double>(frame));

        // 統計が十分貯まっていれば、更新前に外れ値判定する
        // （スパイク自身が平均を押し上げて自己隠蔽するのを避ける）。
        if (rmsCount_ >= config_.spikeWarmupFrames) {
            const double variance = rmsM2_ / static_cast<double>(rmsCount_);
            const double stddev   = std::sqrt(variance);
            if (stddev > 1e-6) {
                const double z = (rms - rmsMean_) / stddev;
                const int64_t frameStartMs = chunkStartMs + static_cast<int64_t>(idx) * frameMs;
                if (z >= config_.volumeSpikeZ &&
                    (lastSpikeMs_ < 0 || frameStartMs - lastSpikeMs_ >= config_.spikeDebounceMs)) {
                    out.push_back(MakeVolumeSpikeEvent(frameStartMs, "laugh_candidate"));
                    lastSpikeMs_ = frameStartMs;
                }
            }
        }

        // Welford によるオンライン平均/分散更新。
        ++rmsCount_;
        const double delta = rms - rmsMean_;
        rmsMean_ += delta / static_cast<double>(rmsCount_);
        rmsM2_   += delta * (rms - rmsMean_);
    }
}

void SignalEventDetector::UpdateSilence(int64_t nowMs, int64_t silenceRunMs, bool speechResumed,
                                        std::vector<nlohmann::json>& out) {
    if (!inSilence_) {
        // 無音が閾値を超えたら開始イベント。開始時刻は無音の始まった地点。
        if (silenceRunMs >= config_.silenceThresholdMs) {
            silenceStartMs_ = nowMs - silenceRunMs;
            if (silenceStartMs_ < 0) {
                silenceStartMs_ = 0;
            }
            out.push_back(MakeSilenceStartEvent(silenceStartMs_));
            inSilence_ = true;
        }
    } else {
        // 無音中に発話が戻った、または無音ランが途切れたら終了イベント。
        if (speechResumed || silenceRunMs < config_.silenceThresholdMs) {
            const int64_t endMs = nowMs - silenceRunMs;
            const int64_t duration = endMs > silenceStartMs_ ? endMs - silenceStartMs_ : 0;
            out.push_back(MakeSilenceEndEvent(endMs, duration));
            inSilence_ = false;
        }
    }
}

void SignalEventDetector::OnSpeechEvent(int64_t timeMs, std::vector<nlohmann::json>& out) {
    speechTimes_.push_back(timeMs);
    // 窓外の古い発話を捨てる。
    while (!speechTimes_.empty() && timeMs - speechTimes_.front() > config_.densityWindowMs) {
        speechTimes_.pop_front();
    }

    if (static_cast<int>(speechTimes_.size()) >= config_.densityBurstCount &&
        (lastDensityMs_ < 0 || timeMs - lastDensityMs_ >= config_.densityCooldownMs)) {
        out.push_back(MakeSpeechDensityEvent(timeMs, static_cast<int>(speechTimes_.size()),
                                             static_cast<int>(config_.densityWindowMs)));
        lastDensityMs_ = timeMs;
    }
}

} // namespace LogGuide
