// Verifies the equalizer actually boosts/cuts the target band and leaves
// far-away frequencies close to untouched, using the already-verified FFT
// to measure it rather than eyeballing waveforms.

#include "audio/equalizer.h"
#include "dsp/fft.h"

#include <algorithm>
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

    // Regression test: bands above Nyquist used to make the biquad
    // unconditionally unstable (w0 >= pi flips alpha negative), blowing the
    // filter state up to Inf/NaN within milliseconds and silencing output.
    // At 22050 Hz, Nyquist is 11025 Hz, so bands 7/8/9 (12k/14k/16k) are
    // above it -- mirrors the ROCK/POP/TREBLE presets that trigger this.
    {
        constexpr int kLowRate = 22050;
        xmad::audio::Equalizer treble;
        treble.Reset(kLowRate, 2);
        treble.SetBandValue(7, 127);
        treble.SetBandValue(8, 127);
        treble.SetBandValue(9, 127);

        std::vector<float> buf(static_cast<size_t>(kN) * 2);
        for (int i = 0; i < kN; ++i) {
            const float s = static_cast<float>(std::sin(2.0 * M_PI * 1000.0 * i / kLowRate));
            buf[static_cast<size_t>(i) * 2] = s;
            buf[static_cast<size_t>(i) * 2 + 1] = s;
        }
        treble.Process(buf.data(), kN);

        bool allFinite = true;
        float peakAbs = 0.0f;
        for (float v : buf) {
            if (!std::isfinite(v)) allFinite = false;
            peakAbs = std::max(peakAbs, std::abs(v));
        }
        if (!allFinite) {
            std::printf("FAIL: 22050Hz treble EQ produced non-finite output\n");
            ok = false;
        }
        if (peakAbs > 100.0f) {
            std::printf("FAIL: 22050Hz treble EQ output magnitude exploded (peak=%.2f)\n", peakAbs);
            ok = false;
        }
    }

    // A band above Nyquist has no content to boost/cut, so it should bypass
    // to a true no-op rather than just "not blow up."
    {
        constexpr int kLowRate = 22050;
        xmad::audio::Equalizer bypassed;
        bypassed.Reset(kLowRate, 2);
        bypassed.SetBandValue(9, 127); // 16000 Hz, above the 11025 Hz Nyquist

        std::vector<float> in(static_cast<size_t>(kN) * 2);
        for (int i = 0; i < kN; ++i) {
            const float s = static_cast<float>(std::sin(2.0 * M_PI * 1000.0 * i / kLowRate));
            in[static_cast<size_t>(i) * 2] = in[static_cast<size_t>(i) * 2 + 1] = s;
        }
        std::vector<float> out = in;
        bypassed.Process(out.data(), kN);

        double maxDiff = 0.0;
        for (size_t i = 0; i < in.size(); ++i) maxDiff = std::max(maxDiff, static_cast<double>(std::abs(out[i] - in[i])));
        if (maxDiff > 1e-6) {
            std::printf("FAIL: bypassed band above Nyquist altered the signal (maxDiff=%.8f)\n", maxDiff);
            ok = false;
        }
    }

    // Exact-Nyquist boundary: at 24000 Hz, band 7 (12000 Hz) sits exactly at
    // Nyquist. A strict '>' check (instead of '>=') would miss this and fall
    // through to the marginally-stable w0==pi case.
    {
        constexpr int kBoundaryRate = 24000;
        xmad::audio::Equalizer boundary;
        boundary.Reset(kBoundaryRate, 2);
        boundary.SetBandValue(7, 127);

        std::vector<float> buf(static_cast<size_t>(kN) * 2);
        for (int i = 0; i < kN; ++i) {
            const float s = static_cast<float>(std::sin(2.0 * M_PI * 1000.0 * i / kBoundaryRate));
            buf[static_cast<size_t>(i) * 2] = s;
            buf[static_cast<size_t>(i) * 2 + 1] = s;
        }
        boundary.Process(buf.data(), kN);

        bool allFinite = true;
        float peakAbs = 0.0f;
        for (float v : buf) {
            if (!std::isfinite(v)) allFinite = false;
            peakAbs = std::max(peakAbs, std::abs(v));
        }
        if (!allFinite) {
            std::printf("FAIL: 24000Hz exact-Nyquist band produced non-finite output\n");
            ok = false;
        }
        if (peakAbs > 100.0f) {
            std::printf("FAIL: 24000Hz exact-Nyquist band output magnitude exploded (peak=%.2f)\n", peakAbs);
            ok = false;
        }
    }

    std::printf(ok ? "PASS\n" : "FAIL\n");
    return ok ? 0 : 1;
}
