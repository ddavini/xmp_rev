// Verifies Reverb's comb/allpass network at its fixed defaults: an impulse
// leaves a real decaying tail (not just an instant echo), silence in means
// silence out, pathological input stays bounded, mono doesn't crash, and
// the two channels decorrelate (stereo spread) given identical L/R input.

#include "audio/reverb.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

using xmad::audio::Reverb;

namespace {
int g_failures = 0;
constexpr int kSampleRate = 44100;
} // namespace

int main() {
    // Impulse response: energy should persist well past the impulse itself
    // (proving feedback/decay, not just an instant dry+wet blend).
    {
        Reverb reverb;
        reverb.Reset(kSampleRate, 2);
        const int frames = kSampleRate / 4; // 250ms
        std::vector<float> buf(static_cast<size_t>(frames) * 2, 0.0f);
        buf[0] = 1.0f;
        buf[1] = 1.0f;
        reverb.Process(buf.data(), static_cast<size_t>(frames));

        // Energy in the 150-200ms window.
        const size_t from = static_cast<size_t>(kSampleRate * 0.15) * 2;
        const size_t to = static_cast<size_t>(kSampleRate * 0.2) * 2;
        double energy = 0.0;
        for (size_t i = from; i < to; ++i) energy += static_cast<double>(buf[i]) * buf[i];
        std::printf("reverb tail energy at 150-200ms: %.8f\n", energy);
        if (energy < 1e-8) {
            std::printf("FAIL: no measurable reverb tail persisted past 150ms\n");
            ++g_failures;
        }
    }

    // All-zero input produces all-zero output (buffers start zeroed, no
    // spontaneous energy).
    {
        Reverb reverb;
        reverb.Reset(kSampleRate, 2);
        std::vector<float> buf(2000, 0.0f);
        reverb.Process(buf.data(), buf.size() / 2);
        for (float v : buf) {
            if (v != 0.0f) {
                std::printf("FAIL: silence did not stay silence (got %f)\n", v);
                ++g_failures;
                break;
            }
        }
    }

    // Pathological inputs stay finite and bounded.
    {
        Reverb reverb;
        reverb.Reset(kSampleRate, 2);
        std::vector<float> buf;
        buf.push_back(1.0f);
        buf.push_back(1.0f); // impulse
        for (int i = 0; i < 4000; ++i) buf.push_back(1.5f); // DC offset above full scale
        for (int i = 0; i < 4000; ++i) buf.push_back(i % 2 == 0 ? 1.0f : -1.0f); // full-scale square
        reverb.Process(buf.data(), buf.size() / 2);
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

    // Mono input doesn't crash.
    {
        Reverb reverb;
        reverb.Reset(kSampleRate, 1);
        std::vector<float> mono(2000, 0.0f);
        mono[0] = 1.0f;
        reverb.Process(mono.data(), mono.size());
        for (float v : mono) {
            if (!std::isfinite(v)) {
                std::printf("FAIL: mono input produced non-finite output\n");
                ++g_failures;
                break;
            }
        }
    }

    // Identical L/R impulse should decorrelate (stereo spread) rather than
    // producing identical L/R tails.
    {
        Reverb reverb;
        reverb.Reset(kSampleRate, 2);
        const int frames = kSampleRate / 10; // 100ms
        std::vector<float> buf(static_cast<size_t>(frames) * 2, 0.0f);
        buf[0] = 1.0f;
        buf[1] = 1.0f;
        reverb.Process(buf.data(), static_cast<size_t>(frames));

        double sumSqDiff = 0.0;
        for (int i = 0; i < frames; ++i) {
            const double d = buf[static_cast<size_t>(i) * 2] - buf[static_cast<size_t>(i) * 2 + 1];
            sumSqDiff += d * d;
        }
        const double rmsDiff = std::sqrt(sumSqDiff / frames);
        std::printf("reverb L-vs-R RMS difference: %.8f\n", rmsDiff);
        if (rmsDiff < 1e-8) {
            std::printf("FAIL: L and R tails are identical - stereo spread not applied\n");
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
