#pragma once

#include <cstddef>

// Fixed-parameter dynamics compressor, the "Compression" entry in the
// Effects menu. Single peak envelope follower (linked across channels, so
// stereo content doesn't shift image under gain reduction), dB-domain gain
// computation - a standard, simple compressor topology. Parameters are
// fixed defaults (no runtime tuning, matching the menu's on/off-only scope):
// threshold -18dBFS, ratio 3:1, attack 10ms, release 100ms, makeup +3dB.

namespace xmad::audio {

class Compressor {
public:
    void Reset(double sampleRate, int channels);

    // In-place, interleaved PCM, channels() as set by Reset().
    void Process(float* interleaved, size_t frameCount);

private:
    double sampleRate_ = 44100.0;
    int channels_ = 2;
    double attackCoeff_ = 0.0;
    double releaseCoeff_ = 0.0;
    double envDb_ = -100.0; // one-pole envelope follower state, in dB
};

} // namespace xmad::audio
