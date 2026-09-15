#include "audio/saturation.h"

#include <algorithm>
#include <cmath>

namespace xmad::audio {

namespace {
constexpr float kDrive = 3.0f;
// tanh(kDrive) precomputed once: for |x| <= 1, |kDrive*x| <= kDrive, so
// dividing by tanh(kDrive) normalizes a full-scale input to exactly
// full-scale output rather than the ~0.995 tanh(3) would otherwise leave on
// the table.
const float kInvTanhDrive = 1.0f / std::tanh(kDrive);
} // namespace

void ApplySaturation(float* interleaved, uint64_t frames, int channels) {
    const uint64_t total = frames * static_cast<uint64_t>(channels);
    for (uint64_t i = 0; i < total; ++i) {
        const float y = std::tanh(kDrive * interleaved[i]) * kInvTanhDrive;
        interleaved[i] = std::clamp(y, -1.0f, 1.0f);
    }
}

} // namespace xmad::audio
