#include "audio/stereo_widen.h"

#include <algorithm>

namespace xmad::audio {

void ApplyStereoWiden(float* interleaved, uint64_t frames, int channels, float width) {
    if (channels != 2) return;
    for (uint64_t i = 0; i < frames; ++i) {
        float& l = interleaved[i * 2];
        float& r = interleaved[i * 2 + 1];
        const float mid = (l + r) * 0.5f;
        const float side = (l - r) * 0.5f * width;
        l = std::clamp(mid + side, -1.0f, 1.0f);
        r = std::clamp(mid - side, -1.0f, 1.0f);
    }
}

} // namespace xmad::audio
