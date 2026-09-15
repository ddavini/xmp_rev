// Verifies ApplySaturation's fixed tanh waveshaper: silence and full-scale
// peaks are exact/hand-checkable, harmonic generation is measured via the
// project's own FFT (same technique as eq_test.cpp), and pathological
// inputs must stay finite/bounded.

#include "audio/saturation.h"
#include "dsp/fft.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

using xmad::audio::ApplySaturation;

namespace {
int g_failures = 0;

void CheckNear(float actual, float expected, float eps, const char* what) {
    if (std::abs(actual - expected) > eps) {
        std::printf("FAIL: %s (got %f, expected %f)\n", what, actual, expected);
        ++g_failures;
    }
}

constexpr int kSampleRate = 44100;
constexpr int kN = 4096;

double MeasureMagnitudeDb(std::vector<float>& interleaved, double toneHz, bool saturate) {
    for (int i = 0; i < kN; ++i) {
        const float s = static_cast<float>(0.5 * std::sin(2.0 * M_PI * toneHz * i / kSampleRate));
        interleaved[static_cast<size_t>(i) * 2] = s;
        interleaved[static_cast<size_t>(i) * 2 + 1] = s;
    }
    if (saturate) ApplySaturation(interleaved.data(), kN, 2);

    xmad::dsp::SpectrumAnalyzer fft(kN, xmad::dsp::Window::Hanning);
    for (int i = 0; i < kN; ++i) fft.Feed(i, interleaved[static_cast<size_t>(i) * 2]);
    std::vector<int> spec(kN / 2);
    fft.Transform(spec.data());
    const int bin = static_cast<int>(toneHz * kN / kSampleRate + 0.5);
    return spec[bin];
}
} // namespace

int main() {
    // Silence stays silence.
    {
        std::vector<float> buf(8, 0.0f);
        ApplySaturation(buf.data(), 4, 2);
        for (float v : buf) CheckNear(v, 0.0f, 1e-6f, "silence stays silence");
    }

    // Full-scale input maps to exactly full-scale output (the drive
    // normalization's whole point).
    {
        std::vector<float> buf = {1.0f, -1.0f};
        ApplySaturation(buf.data(), 1, 2);
        CheckNear(buf[0], 1.0f, 1e-4f, "full-scale +1 stays +1");
        CheckNear(buf[1], -1.0f, 1e-4f, "full-scale -1 stays -1");
    }

    // A half-amplitude 1kHz tone should gain measurable 3rd-harmonic
    // (3kHz) energy after saturation that isn't present (beyond noise
    // floor) in the dry signal.
    {
        std::vector<float> dry(static_cast<size_t>(kN) * 2);
        std::vector<float> wet(static_cast<size_t>(kN) * 2);
        const double dryHarmonic = MeasureMagnitudeDb(dry, 1000.0, false);
        // Re-generate the same dry tone, then saturate it, then measure 3kHz.
        for (int i = 0; i < kN; ++i) {
            const float s = static_cast<float>(0.5 * std::sin(2.0 * M_PI * 1000.0 * i / kSampleRate));
            wet[static_cast<size_t>(i) * 2] = s;
            wet[static_cast<size_t>(i) * 2 + 1] = s;
        }
        ApplySaturation(wet.data(), kN, 2);
        xmad::dsp::SpectrumAnalyzer fft(kN, xmad::dsp::Window::Hanning);
        for (int i = 0; i < kN; ++i) fft.Feed(i, wet[static_cast<size_t>(i) * 2]);
        std::vector<int> spec(kN / 2);
        fft.Transform(spec.data());
        const int bin3k = static_cast<int>(3000.0 * kN / kSampleRate + 0.5);
        const double wetHarmonic = spec[bin3k];

        std::printf("3rd harmonic (3kHz) magnitude: dry baseline unused=%.2f dB, saturated=%.2f dB\n",
                    dryHarmonic, wetHarmonic);
        // The dry signal is a pure tone, so 3kHz should sit near the noise
        // floor (very negative dB); saturation must lift it well above that.
        if (wetHarmonic < -40.0) {
            std::printf("FAIL: saturation did not add measurable 3rd-harmonic energy\n");
            ++g_failures;
        }
    }

    // Pathological inputs stay finite and bounded to [-1, 1].
    {
        std::vector<float> buf;
        for (int i = 0; i < 100; ++i) buf.push_back(2.5f); // DC offset above full scale
        for (int i = 0; i < 100; ++i) buf.push_back(i % 2 == 0 ? 1.0f : -1.0f); // full-scale square
        ApplySaturation(buf.data(), buf.size() / 2, 2);
        bool allFinite = true;
        float peakAbs = 0.0f;
        for (float v : buf) {
            if (!std::isfinite(v)) allFinite = false;
            peakAbs = std::max(peakAbs, std::abs(v));
        }
        if (!allFinite) {
            std::printf("FAIL: pathological input produced non-finite output\n");
            ++g_failures;
        }
        if (peakAbs > 1.0f + 1e-4f) {
            std::printf("FAIL: pathological input produced output above [-1,1] (peak=%.4f)\n", peakAbs);
            ++g_failures;
        }
    }

    if (g_failures == 0) {
        std::printf("PASS\n");
        return 0;
    }
    std::printf("%d FAILURE(S)\n", g_failures);
    return 1;
}
