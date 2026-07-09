#include "analysis/SpeechChunkSplitter.h"

/// stl
#include <cmath>

namespace LogGuide {

int64_t SpeechChunkSplitter::SamplesToMs(int64_t samples) const {
    return samples * 1000 / static_cast<int64_t>(config_.sampleRate);
}

int64_t SpeechChunkSplitter::GetSilenceRunMs() const {
    return SamplesToMs(silenceRunSamples_);
}

void SpeechChunkSplitter::EmitChunk(Chunk& outChunk) {
    outChunk.samples    = std::move(buffer_);
    outChunk.startMs    = SamplesToMs(chunkStartSample_);
    outChunk.durationMs = SamplesToMs(static_cast<int64_t>(outChunk.samples.size()));
    outChunk.hadSpeech  = chunkHadSpeech_;

    buffer_.clear();
    chunkStartSample_ += static_cast<int64_t>(outChunk.samples.size());
    chunkHadSpeech_    = false;
    // silenceRunSamples_ は境界をまたいでも継続させる（無発話区間の連続長を保つ）。
}

bool SpeechChunkSplitter::Feed(const float* samples, size_t count, Chunk& outChunk) {
    // フレーム端数を先頭に連結してから処理する。
    // （count==0 は内部バッファのドレイン用呼び出し。samples は nullptr でもよい）。
    std::vector<float> work;
    work.reserve(frameRemainder_.size() + count);
    work.insert(work.end(), frameRemainder_.begin(), frameRemainder_.end());
    if (samples != nullptr && count > 0) {
        work.insert(work.end(), samples, samples + count);
        consumedSamples_ += static_cast<int64_t>(count);
    }
    frameRemainder_.clear();

    const size_t frame = config_.frameSamples;
    const int64_t minSamples = static_cast<int64_t>(config_.minChunkSec) * config_.sampleRate;
    const int64_t maxSamples = static_cast<int64_t>(config_.maxChunkSec) * config_.sampleRate;
    const int64_t gapSamples = static_cast<int64_t>(config_.gapSec * static_cast<float>(config_.sampleRate));

    size_t pos = 0;
    bool emitted = false;
    while (pos + frame <= work.size()) {
        // フレーム RMS を測る。
        float sumSq = 0.0f;
        for (size_t i = 0; i < frame; ++i) {
            const float s = work[pos + i];
            sumSq += s * s;
        }
        const float rms = std::sqrt(sumSq / static_cast<float>(frame));
        const bool isSpeech = rms >= config_.rmsThreshold;

        // このフレームをチャンクバッファへ積む。
        buffer_.insert(buffer_.end(), work.begin() + pos, work.begin() + pos + frame);
        pos += frame;

        if (isSpeech) {
            chunkHadSpeech_    = true;
            silenceRunSamples_ = 0;
        } else {
            silenceRunSamples_ += static_cast<int64_t>(frame);
        }

        const int64_t bufLen = static_cast<int64_t>(buffer_.size());

        // 最大長に達したら無条件で区切る（強制分割）。
        if (bufLen >= maxSamples) {
            EmitChunk(outChunk);
            emitted = true;
            break;
        }
        // 最小長を超えていて、自然な無音の切れ目に達したら区切る。
        if (bufLen >= minSamples && chunkHadSpeech_ && silenceRunSamples_ >= gapSamples) {
            EmitChunk(outChunk);
            emitted = true;
            break;
        }
    }

    // 未処理のフレーム端数（+ emit で break した場合の残り）を持ち越す。
    if (pos < work.size()) {
        frameRemainder_.assign(work.begin() + pos, work.end());
    }
    return emitted;
}

bool SpeechChunkSplitter::Flush(Chunk& outChunk) {
    // フレーム端数もチャンクへ含めて出し切る。
    if (!frameRemainder_.empty()) {
        buffer_.insert(buffer_.end(), frameRemainder_.begin(), frameRemainder_.end());
        frameRemainder_.clear();
    }
    if (buffer_.empty()) {
        return false;
    }
    EmitChunk(outChunk);
    return true;
}

} // namespace LogGuide
