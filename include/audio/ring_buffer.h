#pragma once

#include <condition_variable>
#include <mutex>
#include <vector>

namespace xmad::audio {

// Interleaved float ring buffer connecting the decode thread (producer,
// may block) to the real-time SDL audio callback (consumer, must never
// block - TryPop uses try_lock and returns fewer frames than requested,
// or zero, rather than wait).
class RingBuffer {
public:
    RingBuffer(size_t capacityFrames, int channels)
        : channels_(channels), capacitySamples_(capacityFrames * channels), buf_(capacitySamples_) {}

    // Producer side. Blocks until all of `frameCount` frames are pushed or
    // Shutdown() is called (in which case it returns early, partial or no
    // data written).
    void Push(const float* data, size_t frameCount) {
        size_t samplesLeft = frameCount * channels_;
        const float* src = data;
        std::unique_lock<std::mutex> lock(mutex_);
        while (samplesLeft > 0 && !shutdown_) {
            notFull_.wait(lock, [&] { return shutdown_ || Free() > 0; });
            if (shutdown_) break;
            const size_t chunk = std::min(samplesLeft, Free());
            for (size_t i = 0; i < chunk; ++i) {
                buf_[writePos_] = src[i];
                writePos_ = (writePos_ + 1) % capacitySamples_;
            }
            count_ += chunk;
            src += chunk;
            samplesLeft -= chunk;
            notEmpty_.notify_one();
        }
    }

    // Consumer side (real-time thread). Never blocks: returns frames
    // actually available up to frameCount (may be 0 under lock contention
    // or an empty buffer - caller should fill the remainder with silence).
    size_t TryPop(float* out, size_t frameCount) {
        std::unique_lock<std::mutex> lock(mutex_, std::try_to_lock);
        if (!lock.owns_lock()) return 0;

        const size_t wantSamples = frameCount * channels_;
        const size_t haveSamples = std::min(wantSamples, count_);
        for (size_t i = 0; i < haveSamples; ++i) {
            out[i] = buf_[readPos_];
            readPos_ = (readPos_ + 1) % capacitySamples_;
        }
        count_ -= haveSamples;
        if (haveSamples > 0) notFull_.notify_one();
        return haveSamples / channels_;
    }

    // Drops all buffered audio (e.g. after a seek).
    void Clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        readPos_ = writePos_ = count_ = 0;
        notFull_.notify_all();
    }

    void Shutdown() {
        std::lock_guard<std::mutex> lock(mutex_);
        shutdown_ = true;
        notFull_.notify_all();
        notEmpty_.notify_all();
    }

private:
    size_t Free() const { return capacitySamples_ - count_; }

    int channels_;
    size_t capacitySamples_;
    std::vector<float> buf_;
    size_t readPos_ = 0, writePos_ = 0, count_ = 0;
    bool shutdown_ = false;
    std::mutex mutex_;
    std::condition_variable notFull_, notEmpty_;
};

} // namespace xmad::audio
