// Verifies ApplyStereoWiden's mid-side math directly - it's a small,
// stateless per-frame formula (unlike the EQ's biquads), so exact expected
// values are hand-computable rather than needing a spectral measurement.

#include "audio/stereo_widen.h"

#include <cmath>
#include <cstdio>
#include <vector>

using xmad::audio::ApplyStereoWiden;

namespace {
int g_failures = 0;

void CheckNear(float actual, float expected, const char* what) {
    if (std::abs(actual - expected) > 1e-5f) {
        std::printf("FAIL: %s (got %f, expected %f)\n", what, actual, expected);
        ++g_failures;
    }
}
} // namespace

int main() {
    // Mono is untouched (channels != 2 is a no-op, per the header's contract).
    {
        std::vector<float> mono = {0.5f, -0.3f, 0.9f};
        ApplyStereoWiden(mono.data(), mono.size(), 1, 3.0f);
        CheckNear(mono[0], 0.5f, "mono passthrough[0]");
        CheckNear(mono[1], -0.3f, "mono passthrough[1]");
        CheckNear(mono[2], 0.9f, "mono passthrough[2]");
    }

    // width=1.0 is the identity transform for any stereo input.
    {
        std::vector<float> stereo = {0.2f, -0.6f, 0.8f, 0.1f};
        ApplyStereoWiden(stereo.data(), 2, 2, 1.0f);
        CheckNear(stereo[0], 0.2f, "width=1 identity L0");
        CheckNear(stereo[1], -0.6f, "width=1 identity R0");
        CheckNear(stereo[2], 0.8f, "width=1 identity L1");
        CheckNear(stereo[3], 0.1f, "width=1 identity R1");
    }

    // Perfectly in-phase (L==R): side energy is zero, so widening never
    // changes anything regardless of width.
    {
        std::vector<float> inPhase = {0.4f, 0.4f};
        ApplyStereoWiden(inPhase.data(), 1, 2, 5.0f);
        CheckNear(inPhase[0], 0.4f, "in-phase L unaffected by widening");
        CheckNear(inPhase[1], 0.4f, "in-phase R unaffected by widening");
    }

    // L=0.5, R=-0.5, width=2.0: mid=0, side=(0.5-(-0.5))*0.5*2=1.0 ->
    // L'=1.0, R'=-1.0 (separation doubled, no clamping needed here).
    {
        std::vector<float> frame = {0.5f, -0.5f};
        ApplyStereoWiden(frame.data(), 1, 2, 2.0f);
        CheckNear(frame[0], 1.0f, "widened L");
        CheckNear(frame[1], -1.0f, "widened R");
    }

    // L=1.0, R=-1.0, width=2.0: mid=0, side=(1-(-1))*0.5*2=2.0 -> would be
    // +/-2.0 unclamped, must come out clamped to +/-1.0.
    {
        std::vector<float> frame = {1.0f, -1.0f};
        ApplyStereoWiden(frame.data(), 1, 2, 2.0f);
        CheckNear(frame[0], 1.0f, "clamped L at +1");
        CheckNear(frame[1], -1.0f, "clamped R at -1");
    }

    if (g_failures == 0) {
        std::printf("PASS\n");
        return 0;
    }
    std::printf("%d FAILURE(S)\n", g_failures);
    return 1;
}
