#include "audio/reverb.h"

#include <algorithm>
#include <cmath>

namespace xmad::audio {

namespace {
constexpr float kFeedback = 0.84f; // room size
constexpr float kDamping = 0.2f;
constexpr float kAllpassG = 0.5f;
constexpr float kMix = 0.25f; // wet fraction

// Freeverb's own tuning constants, in samples @44100Hz - scaled by
// sampleRate/44100 in Reset() for other rates.
constexpr std::array<int, Reverb::kNumCombs> kCombTuning = {1557, 1617, 1491, 1422};
constexpr std::array<int, Reverb::kNumAllpass> kAllpassTuning = {556, 441};
// Right channel's delays are nudged by this many samples (also scaled) so
// its combs/allpasses decorrelate from the left instead of mirroring it.
constexpr int kStereoSpread = 23;
} // namespace

float Reverb::Comb::Process(float x) {
    const float y = buf[idx];
    filterStore = y * (1.0f - kDamping) + filterStore * kDamping;
    buf[idx] = x + filterStore * kFeedback;
    idx = (idx + 1) % buf.size();
    return y;
}

float Reverb::Allpass::Process(float x) {
    const float bufout = buf[idx];
    const float y = -x * kAllpassG + bufout;
    buf[idx] = x + bufout * kAllpassG;
    idx = (idx + 1) % buf.size();
    return y;
}

void Reverb::Reset(double sampleRate, int channels) {
    channels_ = std::clamp(channels, 1, kMaxChannels);
    const double scale = sampleRate / 44100.0;

    for (int ch = 0; ch < kMaxChannels; ++ch) {
        const int spread = ch == 1 ? kStereoSpread : 0;
        for (int c = 0; c < kNumCombs; ++c) {
            const size_t len = static_cast<size_t>(
                std::max(1.0, std::round((kCombTuning[static_cast<size_t>(c)] + spread) * scale)));
            combs_[static_cast<size_t>(ch)][static_cast<size_t>(c)].buf.assign(len, 0.0f);
            combs_[static_cast<size_t>(ch)][static_cast<size_t>(c)].idx = 0;
            combs_[static_cast<size_t>(ch)][static_cast<size_t>(c)].filterStore = 0.0f;
        }
        for (int a = 0; a < kNumAllpass; ++a) {
            const size_t len = static_cast<size_t>(
                std::max(1.0, std::round((kAllpassTuning[static_cast<size_t>(a)] + spread) * scale)));
            allpasses_[static_cast<size_t>(ch)][static_cast<size_t>(a)].buf.assign(len, 0.0f);
            allpasses_[static_cast<size_t>(ch)][static_cast<size_t>(a)].idx = 0;
        }
    }
}

void Reverb::Process(float* interleaved, size_t frameCount) {
    for (size_t i = 0; i < frameCount; ++i) {
        float* frame = interleaved + i * static_cast<size_t>(channels_);
        for (int ch = 0; ch < channels_; ++ch) {
            const float dry = frame[ch];

            float wet = 0.0f;
            for (auto& comb : combs_[static_cast<size_t>(ch)]) wet += comb.Process(dry);
            wet /= static_cast<float>(kNumCombs);

            for (auto& allpass : allpasses_[static_cast<size_t>(ch)]) wet = allpass.Process(wet);

            frame[ch] = std::clamp(dry * (1.0f - kMix) + wet * kMix, -1.0f, 1.0f);
        }
    }
}

} // namespace xmad::audio
