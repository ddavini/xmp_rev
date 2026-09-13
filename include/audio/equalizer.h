#pragma once

#include <array>
#include <atomic>
#include <cstddef>

// 10-band graphic EQ applied to interleaved float PCM in Engine's decode
// thread. The original's EQ curve lived inside xmMP3.dll's closed native
// code (an opaque xmMP3_setEqualizer call) - not recoverable - so this is a
// clean-room implementation using standard RBJ peaking (bell) biquads, one
// cascaded per band. What *is* ported faithfully from frmEQ.frm: the 10
// center frequencies (the xmDFreq labels), the slider range and its
// -12dB..+12dB mapping (the "+12db/0db/-12db" legend in Form_Load), and the
// five preset curves (optEQ_Click's per-band arrays).

namespace xmad::audio {

class Equalizer {
public:
    static constexpr int kBands = 10;
    static constexpr int kMaxChannels = 2;
    static constexpr std::array<double, kBands> kFrequenciesHz = {60,   170,  310,  600,   1000,
                                                                    3000, 6000, 12000, 14000, 16000};

    Equalizer();

    void Reset(double sampleRate, int channels);

    // value: -127..127, matching vsGraphic's xMin/xMax in frmEQ.frm.
    void SetBandValue(int band, int value);
    int BandValue(int band) const { return gains_[static_cast<size_t>(band)].load(); }

    // In-place, interleaved PCM, channels() as set by Reset().
    void Process(float* interleaved, size_t frameCount);

private:
    struct Biquad {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0; // coefficients (a0 pre-normalized to 1)
        double z1 = 0, z2 = 0;                         // Direct Form II state (per channel)
        float ProcessSample(float x);
    };

    void RecomputeBand(int band);

    double sampleRate_ = 44100.0;
    int channels_ = 2;
    std::array<std::atomic<int>, kBands> gains_{};
    std::array<std::array<Biquad, kMaxChannels>, kBands> filters_;
};

} // namespace xmad::audio
