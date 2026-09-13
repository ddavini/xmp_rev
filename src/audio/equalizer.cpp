#include "audio/equalizer.h"

#include <algorithm>
#include <cmath>

namespace xmad::audio {

namespace {
constexpr double kQ = 1.0; // constant bandwidth for all bands; the original's
                            // exact Q lived inside xmMP3.dll's native EQ and
                            // isn't recoverable - this is a reasonable choice
                            // for a 10-band graphic EQ, not a ported value.
}

float Equalizer::Biquad::ProcessSample(float x) {
    // Direct Form II Transposed.
    const double y = b0 * x + z1;
    z1 = b1 * x - a1 * y + z2;
    z2 = b2 * x - a2 * y;
    return static_cast<float>(y);
}

Equalizer::Equalizer() {
    for (int b = 0; b < kBands; ++b) RecomputeBand(b);
}

void Equalizer::Reset(double sampleRate, int channels) {
    sampleRate_ = sampleRate;
    channels_ = std::clamp(channels, 1, kMaxChannels);
    for (auto& bandFilters : filters_) {
        for (auto& f : bandFilters) {
            f.z1 = f.z2 = 0.0;
        }
    }
    for (int b = 0; b < kBands; ++b) RecomputeBand(b);
}

void Equalizer::SetBandValue(int band, int value) {
    if (band < 0 || band >= kBands) return;
    value = std::clamp(value, -127, 127);
    gains_[static_cast<size_t>(band)] = value;
    RecomputeBand(band);
}

void Equalizer::RecomputeBand(int band) {
    // -127..127 -> -12dB..+12dB, per frmEQ.frm's Form_Load legend.
    const double gainDb = (gains_[static_cast<size_t>(band)].load() / 127.0) * 12.0;

    // RBJ Audio EQ Cookbook peaking filter.
    const double A = std::pow(10.0, gainDb / 40.0);
    const double w0 = 2.0 * M_PI * kFrequenciesHz[static_cast<size_t>(band)] / sampleRate_;
    const double alpha = std::sin(w0) / (2.0 * kQ);
    const double cosw0 = std::cos(w0);

    const double a0 = 1.0 + alpha / A;
    const double b0 = (1.0 + alpha * A) / a0;
    const double b1 = (-2.0 * cosw0) / a0;
    const double b2 = (1.0 - alpha * A) / a0;
    const double a1 = (-2.0 * cosw0) / a0;
    const double a2 = (1.0 - alpha / A) / a0;

    for (auto& f : filters_[static_cast<size_t>(band)]) {
        f.b0 = b0;
        f.b1 = b1;
        f.b2 = b2;
        f.a1 = a1;
        f.a2 = a2;
    }
}

void Equalizer::Process(float* interleaved, size_t frameCount) {
    for (size_t i = 0; i < frameCount; ++i) {
        for (int ch = 0; ch < channels_; ++ch) {
            float sample = interleaved[i * static_cast<size_t>(channels_) + static_cast<size_t>(ch)];
            for (int b = 0; b < kBands; ++b) {
                if (gains_[static_cast<size_t>(b)].load() == 0) continue; // skip neutral bands
                sample = filters_[static_cast<size_t>(b)][static_cast<size_t>(ch)].ProcessSample(sample);
            }
            interleaved[i * static_cast<size_t>(channels_) + static_cast<size_t>(ch)] = sample;
        }
    }
}

} // namespace xmad::audio
