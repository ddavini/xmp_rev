// Verifies Compressor's gain-reduction behavior against its fixed defaults
// (threshold -18dBFS, ratio 3:1, attack 10ms, release 100ms, makeup +3dB):
// a quiet signal settles near +makeup, a loud signal is measurably reduced
// vs. a naive +makeup-only boost, the attack lags the eventual steady
// state, and pathological input stays finite/bounded.

#include "audio/compressor.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

using xmad::audio::Compressor;

namespace {
int g_failures = 0;
constexpr int kSampleRate = 44100;

std::vector<float> MakeSineStereo(double amplitude, double hz, int frames) {
    std::vector<float> buf(static_cast<size_t>(frames) * 2);
    for (int i = 0; i < frames; ++i) {
        const float s = static_cast<float>(amplitude * std::sin(2.0 * M_PI * hz * i / kSampleRate));
        buf[static_cast<size_t>(i) * 2] = s;
        buf[static_cast<size_t>(i) * 2 + 1] = s;
    }
    return buf;
}

float PeakAbs(const std::vector<float>& buf, size_t fromFrame, size_t toFrame) {
    float peak = 0.0f;
    for (size_t i = fromFrame * 2; i < toFrame * 2; ++i) peak = std::max(peak, std::abs(buf[i]));
    return peak;
}
} // namespace

int main() {
    // Quiet signal (-30dBFS, well below the -18dBFS threshold): after
    // attack/release settle, output should sit near input * 10^(makeup/20)
    // (~+3dB boost, no gain reduction applied).
    {
        Compressor comp;
        comp.Reset(kSampleRate, 2);
        const double amp = std::pow(10.0, -30.0 / 20.0);
        auto buf = MakeSineStereo(amp, 1000.0, kSampleRate); // 1 second, plenty of settle time
        comp.Process(buf.data(), buf.size() / 2);

        const float inPeak = static_cast<float>(amp);
        const float outPeak = PeakAbs(buf, buf.size() / 2 - static_cast<size_t>(kSampleRate) / 20, buf.size() / 2);
        const double gainDb = 20.0 * std::log10(outPeak / inPeak);
        std::printf("quiet signal steady-state gain: %.2f dB (expect ~+3)\n", gainDb);
        if (gainDb < 2.0 || gainDb > 4.0) {
            std::printf("FAIL: quiet signal not boosted by ~makeup gain\n");
            ++g_failures;
        }
    }

    // Loud signal (0dBFS, well above threshold): steady-state output peak
    // should be measurably below naive +makeup-only (i.e. below input*10^(3/20)),
    // proving real gain reduction is applied.
    {
        Compressor comp;
        comp.Reset(kSampleRate, 2);
        auto buf = MakeSineStereo(1.0, 1000.0, kSampleRate);
        comp.Process(buf.data(), buf.size() / 2);

        const float outPeak = PeakAbs(buf, buf.size() / 2 - static_cast<size_t>(kSampleRate) / 20, buf.size() / 2);
        const float naiveBoost = static_cast<float>(std::pow(10.0, 3.0 / 20.0)); // +3dB, no reduction
        std::printf("loud signal steady-state peak: %.4f (naive +makeup-only would be %.4f)\n", outPeak,
                    naiveBoost);
        if (outPeak >= naiveBoost) {
            std::printf("FAIL: loud signal shows no measurable gain reduction\n");
            ++g_failures;
        }
    }

    // Attack lag: right after a sudden loud (0dBFS) onset, gain reduction
    // in the first few ms should be less than the eventual steady-state
    // reduction (i.e. the very first samples are louder, relatively, than
    // once the envelope has caught up).
    {
        Compressor comp;
        comp.Reset(kSampleRate, 2);
        auto buf = MakeSineStereo(1.0, 1000.0, kSampleRate / 10); // 100ms
        comp.Process(buf.data(), buf.size() / 2);

        // First ~2ms vs. last ~20ms.
        const float earlyPeak = PeakAbs(buf, 0, 88);
        const float latePeak = PeakAbs(buf, buf.size() / 2 - 882, buf.size() / 2);
        std::printf("attack: early peak=%.4f, late (steady-state) peak=%.4f\n", earlyPeak, latePeak);
        if (earlyPeak <= latePeak) {
            std::printf("FAIL: attack didn't lag - early samples should be louder than steady state\n");
            ++g_failures;
        }
    }

    // Silence stays silence.
    {
        Compressor comp;
        comp.Reset(kSampleRate, 2);
        std::vector<float> buf(2000, 0.0f);
        comp.Process(buf.data(), buf.size() / 2);
        for (float v : buf) {
            if (std::abs(v) > 1e-6f) {
                std::printf("FAIL: silence did not stay silence (got %f)\n", v);
                ++g_failures;
                break;
            }
        }
    }

    // Pathological inputs stay finite and bounded.
    {
        Compressor comp;
        comp.Reset(kSampleRate, 2);
        std::vector<float> buf;
        for (int i = 0; i < 200; ++i) buf.push_back(1.5f); // DC offset above full scale
        for (int i = 0; i < 200; ++i) buf.push_back(i % 2 == 0 ? 1.0f : -1.0f); // full-scale square
        comp.Process(buf.data(), buf.size() / 2);
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
