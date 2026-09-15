#include "audio/compressor.h"

#include <algorithm>
#include <cmath>

namespace xmad::audio {

namespace {
constexpr double kThresholdDb = -18.0;
constexpr double kRatio = 3.0;
constexpr double kAttackMs = 10.0;
constexpr double kReleaseMs = 100.0;
constexpr double kMakeupDb = 3.0;
constexpr double kMinDb = -100.0; // floor for the log of a near-silent peak
} // namespace

void Compressor::Reset(double sampleRate, int channels) {
    sampleRate_ = sampleRate;
    channels_ = std::clamp(channels, 1, 2);
    attackCoeff_ = std::exp(-1.0 / (sampleRate_ * kAttackMs / 1000.0));
    releaseCoeff_ = std::exp(-1.0 / (sampleRate_ * kReleaseMs / 1000.0));
    envDb_ = kMinDb;
}

void Compressor::Process(float* interleaved, size_t frameCount) {
    for (size_t i = 0; i < frameCount; ++i) {
        float* frame = interleaved + i * static_cast<size_t>(channels_);

        float peak = 0.0f;
        for (int ch = 0; ch < channels_; ++ch) peak = std::max(peak, std::abs(frame[ch]));
        const double xDb = 20.0 * std::log10(std::max(static_cast<double>(peak), 1e-6));

        const double coeff = xDb > envDb_ ? attackCoeff_ : releaseCoeff_;
        envDb_ = coeff * envDb_ + (1.0 - coeff) * xDb;

        double gainReductionDb = 0.0;
        if (envDb_ > kThresholdDb) gainReductionDb = (kThresholdDb - envDb_) * (1.0 - 1.0 / kRatio);
        const float gainLinear = static_cast<float>(std::pow(10.0, (gainReductionDb + kMakeupDb) / 20.0));

        for (int ch = 0; ch < channels_; ++ch) frame[ch] = std::clamp(frame[ch] * gainLinear, -1.0f, 1.0f);
    }
}

} // namespace xmad::audio
