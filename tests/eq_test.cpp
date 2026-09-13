// Verifies the equalizer actually boosts/cuts the target band and leaves
// far-away frequencies close to untouched, using the already-verified FFT
// to measure it rather than eyeballing waveforms.

#include "audio/equalizer.h"
#include "dsp/fft.h"

#include <cmath>
#include <cstdio>
#include <vector>

namespace {

constexpr int kSampleRate = 44100;
constexpr int kN = 4096; // FFT window

double MeasureMagnitudeDb(xmad::audio::Equalizer& eq, double toneHz) {
    std::vector<float> interleaved(static_cast<size_t>(kN) * 2); // stereo
    for (int i = 0; i < kN; ++i) {
        const float s = static_cast<float>(std::sin(2.0 * M_PI * toneHz * i / kSampleRate));
        interleaved[static_cast<size_t>(i) * 2] = s;
        interleaved[static_cast<size_t>(i) * 2 + 1] = s;
    }
    eq.Process(interleaved.data(), kN);

    xmad::dsp::SpectrumAnalyzer fft(kN, xmad::dsp::Window::Hanning);
    for (int i = 0; i < kN; ++i) fft.Feed(i, interleaved[static_cast<size_t>(i) * 2]);
    std::vector<int> spec(kN / 2);
    fft.Transform(spec.data());

    int peak = 0;
    for (int i = 1; i < kN / 2; ++i)
        if (spec[i] > spec[peak]) peak = i;
    return spec[peak]; // already in our FFT's dB-ish scale (20*log10)
}

} // namespace

int main() {
    bool ok = true;

    // Band 4 = 1000 Hz (see kFrequenciesHz). Flat EQ baseline first.
    xmad::audio::Equalizer flat;
    flat.Reset(kSampleRate, 2);
    const double baseline1k = MeasureMagnitudeDb(flat, 1000.0);
    const double baseline60 = MeasureMagnitudeDb(flat, 60.0);

    xmad::audio::Equalizer boosted;
    boosted.Reset(kSampleRate, 2);
    boosted.SetBandValue(4, 127); // max boost, +12dB, on the 1kHz band
    const double boosted1k = MeasureMagnitudeDb(boosted, 1000.0);
    const double boosted60 = MeasureMagnitudeDb(boosted, 60.0);

    const double delta1k = boosted1k - baseline1k;
    const double delta60 = boosted60 - baseline60;
    std::printf("1kHz band +127: delta at 1kHz = %.2f dB (expect ~+12), delta at 60Hz = %.2f dB (expect ~0)\n",
                delta1k, delta60);

    if (delta1k < 9.0 || delta1k > 15.0) {
        std::printf("FAIL: boost at target frequency not close to +12dB\n");
        ok = false;
    }
    if (std::abs(delta60) > 2.0) {
        std::printf("FAIL: boosting 1kHz band leaked too much into 60Hz\n");
        ok = false;
    }

    // Cut should go the other way.
    xmad::audio::Equalizer cut;
    cut.Reset(kSampleRate, 2);
    cut.SetBandValue(4, -127);
    const double cut1k = MeasureMagnitudeDb(cut, 1000.0);
    const double deltaCut = cut1k - baseline1k;
    std::printf("1kHz band -127: delta at 1kHz = %.2f dB (expect ~-12)\n", deltaCut);
    if (deltaCut > -9.0 || deltaCut < -15.0) {
        std::printf("FAIL: cut at target frequency not close to -12dB\n");
        ok = false;
    }

    std::printf(ok ? "PASS\n" : "FAIL\n");
    return ok ? 0 : 1;
}
