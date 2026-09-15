#pragma once

#include <array>
#include <cstddef>
#include <vector>

// Fixed-parameter Freeverb-style reverb, the "Reverb" entry in the Effects
// menu. Per channel: 4 parallel damped comb filters feeding 2 series
// allpass filters. Channel 1 (right) uses delay lengths offset from
// channel 0 (left) by a fixed "stereo spread" so the two channels
// decorrelate instead of reverbing identically. Fixed defaults (no runtime
// tuning): feedback (room size) 0.84, damping 0.2, 25% wet mix.

namespace xmad::audio {

class Reverb {
public:
    static constexpr int kNumCombs = 4;
    static constexpr int kNumAllpass = 2;
    static constexpr int kMaxChannels = 2;

    void Reset(double sampleRate, int channels);

    // In-place, interleaved PCM, channels() as set by Reset().
    void Process(float* interleaved, size_t frameCount);

private:
    struct Comb {
        std::vector<float> buf;
        size_t idx = 0;
        float filterStore = 0.0f;
        float Process(float x);
    };
    struct Allpass {
        std::vector<float> buf;
        size_t idx = 0;
        float Process(float x);
    };

    int channels_ = 2;
    std::array<std::array<Comb, kNumCombs>, kMaxChannels> combs_;
    std::array<std::array<Allpass, kNumAllpass>, kMaxChannels> allpasses_;
};

} // namespace xmad::audio
