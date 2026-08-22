#include "analysis/VideoAnalysisPipeline.h"

/// module
#include "analysis/TimelineJsonlWriter.h"
#include "analysis/VideoSignalDetector.h"

/// engine
#include "logger/Logger.h"

/// stl
#include <algorithm>

namespace LogGuide {

namespace {

// 1 つのダウンスケール先ピクセルあたり、ソースから取るサンプル数の上限（軸ごと）。
// ボックス平均で全画素を舐めると 4K で 800 万回になるので、間引いて上限を設ける。
// 4x4=16 タップあれば、単純な最近傍と違いエイリアシング由来の偽差分は十分に消える。
constexpr uint32_t kMaxTapsPerAxis = 4;

} // namespace

VideoAnalysisPipeline::VideoAnalysisPipeline()  = default;
VideoAnalysisPipeline::~VideoAnalysisPipeline() = default;

bool VideoAnalysisPipeline::Initialize(const VideoAnalysisTuning& tuning,
                                       std::vector<std::string>* warnings) {
    tuning_  = tuning;
    enabled_ = tuning.enabled;

    if (!enabled_) {
        if (warnings) {
            warnings->push_back("video analysis disabled in logguide.toml");
        }
        return false;
    }
    if (tuning_.downscaleWidth < 16 || tuning_.sampleFps < 1) {
        enabled_ = false;
        if (warnings) {
            warnings->push_back("invalid video_analysis settings (downscale_width/sample_fps); video analysis disabled");
        }
        return false;
    }

    sampleIntervalMs_ = 1000 / tuning_.sampleFps;
    LOG_INFO("VideoAnalysisPipeline: initialized ({}fps, width={}, static_threshold={})",
             tuning_.sampleFps, tuning_.downscaleWidth, tuning_.staticThreshold);
    return true;
}

bool VideoAnalysisPipeline::Start(std::shared_ptr<TimelineJsonlWriter> writer) {
    if (!enabled_ || running_.load() || !writer) {
        return false;
    }

    VideoSignalDetector::Config cfg;
    cfg.staticThreshold      = tuning_.staticThreshold;
    cfg.staticDurationMs     = tuning_.staticDurationMs;
    cfg.staticExitMs         = tuning_.staticExitMs;
    cfg.blankLumaThreshold   = tuning_.blankLumaThreshold;
    cfg.blankDurationMs      = tuning_.blankDurationMs;
    cfg.transitionZ          = tuning_.transitionZ;
    cfg.transitionDebounceMs = tuning_.transitionDebounceMs;
    cfg.thrashWindowMs       = tuning_.thrashWindowMs;
    cfg.thrashCount          = tuning_.thrashCount;
    cfg.thrashCooldownMs     = tuning_.thrashCooldownMs;
    cfg.warmupSamples        = tuning_.warmupSamples;

    std::lock_guard<std::mutex> lock(mutex_);
    detector_ = std::make_unique<VideoSignalDetector>(cfg);
    writer_   = std::move(writer);

    luma_.clear();
    dstWidth_     = 0;
    dstHeight_    = 0;
    nextSampleMs_ = 0;
    lastTimeMs_   = 0;
    startClock_   = std::chrono::steady_clock::now();
    samples_.store(0);
    events_.store(0);
    lastDiff_.store(0.0);

    running_.store(true);
    LOG_INFO("VideoAnalysisPipeline: started");
    return true;
}

void VideoAnalysisPipeline::Downscale(const uint8_t* bgra, uint32_t width, uint32_t height,
                                      uint32_t stride) {
    for (uint32_t dy = 0; dy < dstHeight_; ++dy) {
        const uint32_t sy0 = dy * height / dstHeight_;
        const uint32_t sy1 = (std::max)(sy0 + 1u, (dy + 1) * height / dstHeight_);
        const uint32_t stepY = (std::max)(1u, (sy1 - sy0) / kMaxTapsPerAxis);

        for (uint32_t dx = 0; dx < dstWidth_; ++dx) {
            const uint32_t sx0 = dx * width / dstWidth_;
            const uint32_t sx1 = (std::max)(sx0 + 1u, (dx + 1) * width / dstWidth_);
            const uint32_t stepX = (std::max)(1u, (sx1 - sx0) / kMaxTapsPerAxis);

            uint32_t taps = 0;
            float    sum  = 0.0f;
            for (uint32_t sy = sy0; sy < sy1; sy += stepY) {
                const uint8_t* row = bgra + static_cast<size_t>(sy) * stride;
                for (uint32_t sx = sx0; sx < sx1; sx += stepX) {
                    const uint8_t* p = row + static_cast<size_t>(sx) * 4;
                    // BT.601 輝度。BGRA 順（Desktop Duplication / GDI とも B8G8R8A8）。
                    sum += 0.114f * p[0] + 0.587f * p[1] + 0.299f * p[2];
                    ++taps;
                }
            }
            luma_[static_cast<size_t>(dy) * dstWidth_ + dx] =
                taps > 0 ? sum / (static_cast<float>(taps) * 255.0f) : 0.0f;
        }
    }
}

void VideoAnalysisPipeline::OnFrame(const uint8_t* bgra, uint32_t width, uint32_t height,
                                    uint32_t stride) {
    if (!running_.load() || !bgra || width == 0 || height == 0) {
        return;
    }

    // レート制限は lock の外で済ませる（大半のフレームはここで戻る）。
    const auto now = std::chrono::steady_clock::now();
    const int64_t timeMs =
        std::chrono::duration_cast<std::chrono::milliseconds>(now - startClock_).count();
    if (timeMs < nextSampleMs_) {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (!running_.load() || !detector_ || !writer_) {
        return;
    }
    nextSampleMs_ = timeMs + sampleIntervalMs_;
    lastTimeMs_   = timeMs;

    // 解像度が変わったらバッファを張り直す（検出器側も差分をスキップして再同期する）。
    const uint32_t dw = (std::min)(static_cast<uint32_t>(tuning_.downscaleWidth), width);
    const uint32_t dh = (std::max)(1u, dw * height / width);
    if (dw != dstWidth_ || dh != dstHeight_) {
        dstWidth_  = dw;
        dstHeight_ = dh;
        luma_.assign(static_cast<size_t>(dw) * dh, 0.0f);
    }

    Downscale(bgra, width, height, stride);

    std::vector<nlohmann::json> events;
    detector_->ProcessSample(timeMs, luma_, events);
    lastDiff_.store(detector_->GetLastDiff());
    samples_.fetch_add(1);

    for (const auto& e : events) {
        writer_->Write(e);
    }
    if (!events.empty()) {
        events_.fetch_add(events.size());
    }
}

void VideoAnalysisPipeline::Stop() {
    if (!running_.load()) {
        return;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    running_.store(false);

    if (detector_ && writer_) {
        std::vector<nlohmann::json> events;
        detector_->Flush(lastTimeMs_, events);
        for (const auto& e : events) {
            writer_->Write(e);
        }
        events_.fetch_add(events.size());
    }
    detector_.reset();
    writer_.reset(); // ライタの Close は所有者（音声側 / RecordingSystem）が行う

    LOG_INFO("VideoAnalysisPipeline: stopped (samples={}, events={})",
             samples_.load(), events_.load());
}

VideoAnalysisPipeline::Status VideoAnalysisPipeline::GetStatus() const {
    Status s;
    s.enabled  = enabled_;
    s.running  = running_.load();
    s.samples  = samples_.load();
    s.events   = events_.load();
    s.lastDiff = lastDiff_.load();
    return s;
}

} // namespace LogGuide
