#pragma once

#include <array>
#include <cstddef>
#include <vector>

// Fixed-parameter stereo chorus, the "Chorus" entry in the Effects menu.
// Per-channel modulated delay line (LFO-swept read position into a circular
// buffer), right channel's LFO phase offset by 90 degrees from the left for
// stereo width. Fixed defaults (no runtime tuning): base delay 20ms, depth
// +/-4ms, rate 0.8Hz, 35% wet mix, no feedback.

namespace xmad::audio {

class Chorus {
public:
    static constexpr int kMaxChannels = 2;

    void Reset(double sampleRate, int channels);

    // In-place, interleaved PCM, channels() as set by Reset().
    void Process(float* interleaved, size_t frameCount);

private:
    struct Line {
        std::vector<float> buf;
        size_t writeIdx = 0;
    };

    double sampleRate_ = 44100.0;
    int channels_ = 2;
    double phase_ = 0.0; // shared running LFO phase, radians, wrapped to [0, 2*pi)
    std::array<Line, kMaxChannels> lines_;
};

} // namespace xmad::audio
