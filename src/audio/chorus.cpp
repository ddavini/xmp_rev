#include "audio/chorus.h"

#include <algorithm>
#include <cmath>

namespace xmad::audio {

namespace {
constexpr double kBaseDelayMs = 20.0;
constexpr double kDepthMs = 4.0;
constexpr double kRateHz = 0.8;
constexpr float kMix = 0.35f;
// Right channel's LFO runs a quarter-cycle ahead of the left, the classic
// stereo-chorus trick for widening an otherwise mono modulation.
constexpr std::array<double, Chorus::kMaxChannels> kPhaseOffset = {0.0, M_PI / 2.0};
} // namespace

void Chorus::Reset(double sampleRate, int channels) {
    sampleRate_ = sampleRate;
    channels_ = std::clamp(channels, 1, kMaxChannels);
    phase_ = 0.0;

    // Headroom above base+depth so the sweep never reads outside the buffer.
    const size_t bufSize =
        static_cast<size_t>(std::ceil((kBaseDelayMs + kDepthMs + 5.0) * sampleRate_ / 1000.0));
    for (auto& line : lines_) {
        line.buf.assign(bufSize, 0.0f);
        line.writeIdx = 0;
    }
}

void Chorus::Process(float* interleaved, size_t frameCount) {
    for (size_t i = 0; i < frameCount; ++i) {
        float* frame = interleaved + i * static_cast<size_t>(channels_);

        phase_ += 2.0 * M_PI * kRateHz / sampleRate_;
        if (phase_ > 2.0 * M_PI) phase_ -= 2.0 * M_PI;

        for (int ch = 0; ch < channels_; ++ch) {
            Line& line = lines_[static_cast<size_t>(ch)];
            const size_t bufSize = line.buf.size();

            const float dry = frame[ch];
            line.buf[line.writeIdx] = dry;

            const double lfo = std::sin(phase_ + kPhaseOffset[static_cast<size_t>(ch)]);
            const double delayMs = kBaseDelayMs + kDepthMs * lfo;
            const double delaySamples = delayMs * sampleRate_ / 1000.0;

            double readPos = static_cast<double>(line.writeIdx) - delaySamples;
            while (readPos < 0.0) readPos += static_cast<double>(bufSize);

            const size_t idx0 = static_cast<size_t>(readPos) % bufSize;
            const size_t idx1 = (idx0 + 1) % bufSize;
            const double frac = readPos - std::floor(readPos);
            const float wet = static_cast<float>(line.buf[idx0] * (1.0 - frac) + line.buf[idx1] * frac);

            frame[ch] = dry * (1.0f - kMix) + wet * kMix;

            line.writeIdx = (line.writeIdx + 1) % bufSize;
        }
    }
}

} // namespace xmad::audio
