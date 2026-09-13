// Covers LevelGradientColor against the exact pixel values extracted from
// the original's PICGPH resource (see gfx/level_meter.h) - a handful of
// known rows, plus the endpoints and clamping behavior.

#include "gfx/level_meter.h"

#include <cstdio>

using xmad::gfx::LevelGradientColor;
using xmad::gfx::RGB;

namespace {
int g_failures = 0;

void CheckColor(RGB actual, RGB expected, const char* what) {
    if (actual.r != expected.r || actual.g != expected.g || actual.b != expected.b) {
        std::printf("FAIL: %s (got rgb(%d,%d,%d), expected rgb(%d,%d,%d))\n", what, actual.r, actual.g, actual.b,
                    expected.r, expected.g, expected.b);
        ++g_failures;
    }
}
} // namespace

int main() {
    // Bottom of the meter: pure green, matching PICGPH's bottom rows exactly.
    CheckColor(LevelGradientColor(0.0f), RGB{0, 230, 0}, "position=0.0 (bottom) is pure green");

    // Top of the meter: pure yellow, matching PICGPH's top rows exactly.
    CheckColor(LevelGradientColor(1.0f), RGB{230, 230, 0}, "position=1.0 (top) is pure yellow");

    // A couple of interior rows, checked against the exact extracted
    // pixel data (row = round((1-position)*25)): position=0.5 -> row 13
    // (R=115); position=0.2 -> row 20 (R=22).
    CheckColor(LevelGradientColor(0.5f), RGB{115, 230, 0}, "position=0.5 matches extracted row 13");
    CheckColor(LevelGradientColor(0.2f), RGB{22, 230, 0}, "position=0.2 matches extracted row 20");

    // The bottom ~15% and top ~15% are flat (not part of the gradient
    // ramp) in the real asset - confirm that plateau survives here too.
    CheckColor(LevelGradientColor(0.05f), RGB{0, 230, 0}, "still in the flat green plateau near the bottom");
    CheckColor(LevelGradientColor(0.95f), RGB{230, 230, 0}, "still in the flat yellow plateau near the top");

    // Out-of-range input is clamped, not undefined behavior.
    CheckColor(LevelGradientColor(-0.5f), RGB{0, 230, 0}, "negative position clamps to bottom");
    CheckColor(LevelGradientColor(1.5f), RGB{230, 230, 0}, "position > 1 clamps to top");

    if (g_failures == 0) {
        std::printf("PASS\n");
        return 0;
    }
    std::printf("%d FAILURE(S)\n", g_failures);
    return 1;
}
