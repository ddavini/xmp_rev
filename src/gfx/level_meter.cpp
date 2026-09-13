#include "gfx/level_meter.h"

#include <algorithm>
#include <cmath>

namespace xmad::gfx {

namespace {
// Red channel at each of PICGPH's 26 rows, top (index 0) to bottom (index
// 25) - green (230) and blue (0) are constant across the whole strip, only
// red varies. Extracted directly from the resource, not re-derived.
constexpr int kRows = 26;
constexpr uint8_t kRedByRow[kRows] = {230, 230, 230, 230, 219, 207, 196, 184, 172, 161, 149, 137,
                                       125, 115, 102, 88,  75,  61,  49,  35,  22,  8,   0,   0,
                                       0,   0};
} // namespace

RGB LevelGradientColor(float position) {
    const float clamped = std::clamp(position, 0.0f, 1.0f);
    // position 0 (bottom) -> row 25; position 1 (top) -> row 0.
    const int row = std::clamp(static_cast<int>(std::lround((1.0f - clamped) * (kRows - 1))), 0, kRows - 1);
    return RGB{kRedByRow[row], 230, 0};
}

} // namespace xmad::gfx
