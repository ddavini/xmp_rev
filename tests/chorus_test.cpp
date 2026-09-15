// Verifies Chorus produces an audibly different, bounded, stereo-widened
// signal at its fixed defaults - no runtime tuning to test against exact
// values (unlike stereo_widen's per-frame algebra), so these are behavioral
// checks: wet != dry, mono doesn't crash, output stays bounded, and L/R
// diverge given identical L/R input (the 90-degree LFO phase offset).

#include "audio/chorus.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

using xmad::audio::Chorus;

namespace {
int g_failures = 0;
constexpr int kSampleRate = 44100;

std::vector<float> MakeSineStereo(double hz, int frames) {
    std::vector<float> buf(static_cast<size_t>(frames) * 2);
    for (int i = 0; i < frames; ++i) {
        const float s = static_cast<float>(0.5 * std::sin(2.0 * M_PI * hz * i / kSampleRate));
        buf[static_cast<size_t>(i) * 2] = s;
        buf[static_cast<size_t>(i) * 2 + 1] = s;
    }
    return buf;
}
} // namespace

int main() {
    // Wet output should differ measurably from the dry input.
    {
        Chorus chorus;
        chorus.Reset(kSampleRate, 2);
        auto dry = MakeSineStereo(440.0, kSampleRate / 2);
        auto wet = dry;
        chorus.Process(wet.data(), wet.size() / 2);

        double sumSqDiff = 0.0;
        for (size_t i = 0; i < dry.size(); ++i) {
            const double d = wet[i] - dry[i];
            sumSqDiff += d * d;
        }
        const double rmsDiff = std::sqrt(sumSqDiff / static_cast<double>(dry.size()));
        std::printf("chorus wet-vs-dry RMS difference: %.5f\n", rmsDiff);
        if (rmsDiff < 1e-4) {
            std::printf("FAIL: chorus output not audibly different from dry input\n");
            ++g_failures;
        }
    }

    // Mono input doesn't crash and stays finite.
    {
        Chorus chorus;
        chorus.Reset(kSampleRate, 1);
        std::vector<float> mono(2000);
        for (size_t i = 0; i < mono.size(); ++i)
            mono[i] = static_cast<float>(0.5 * std::sin(2.0 * M_PI * 440.0 * static_cast<double>(i) / kSampleRate));
        chorus.Process(mono.data(), mono.size());
        for (float v : mono) {
            if (!std::isfinite(v)) {
                std::printf("FAIL: mono input produced non-finite output\n");
                ++g_failures;
                break;
            }
        }
    }

    // Pathological inputs stay finite and bounded (the wet/dry blend is a
    // convex combination, so peak output should not exceed peak input).
    {
        Chorus chorus;
        chorus.Reset(kSampleRate, 2);
        std::vector<float> buf;
        for (int i = 0; i < 200; ++i) buf.push_back(1.0f); // DC offset at full scale
        for (int i = 0; i < 200; ++i) buf.push_back(i % 2 == 0 ? 1.0f : -1.0f); // full-scale square
        for (int i = 0; i < 200; ++i) buf.push_back(0.0f); // silence
        chorus.Process(buf.data(), buf.size() / 2);
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

    // Identical L/R input should produce different L vs. R output, proving
    // the stereo phase offset is actually applied (otherwise chorus would
    // collapse mono content right back to mono).
    {
        Chorus chorus;
        chorus.Reset(kSampleRate, 2);
        auto buf = MakeSineStereo(440.0, kSampleRate / 4);
        chorus.Process(buf.data(), buf.size() / 2);

        double sumSqDiff = 0.0;
        for (size_t i = 0; i < buf.size() / 2; ++i) {
            const double d = buf[i * 2] - buf[i * 2 + 1];
            sumSqDiff += d * d;
        }
        const double rmsDiff = std::sqrt(sumSqDiff / static_cast<double>(buf.size() / 2));
        std::printf("chorus L-vs-R RMS difference: %.5f\n", rmsDiff);
        if (rmsDiff < 1e-4) {
            std::printf("FAIL: L and R channels are identical - stereo phase offset not applied\n");
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
