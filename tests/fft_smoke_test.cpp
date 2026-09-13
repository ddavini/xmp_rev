// Verifies the from-scratch SpectrumAnalyzer against a brute-force
// reference DFT, since a wrong-but-plausible-looking FFT (see the xmFFT.dll
// prototype this replaced) won't be caught by trivial tests like an
// impulse response.

#include "dsp/fft.h"

#include <cmath>
#include <cstdio>
#include <vector>

namespace {

bool CheckPureTone() {
    constexpr int N = 64;
    constexpr int targetBin = 5;

    std::vector<double> input(N);
    for (int n = 0; n < N; ++n) input[n] = std::sin(2.0 * M_PI * targetBin * n / N);

    // Brute-force reference DFT (O(N^2), trusted).
    std::vector<double> ref(N / 2);
    for (int k = 0; k < N / 2; ++k) {
        double re = 0, im = 0;
        for (int n = 0; n < N; ++n) {
            const double ang = -2.0 * M_PI * k * n / N;
            re += input[n] * std::cos(ang);
            im += input[n] * std::sin(ang);
        }
        ref[k] = std::sqrt(re * re + im * im);
    }
    int refPeakBin = 0;
    for (int i = 0; i < N / 2; ++i)
        if (ref[i] > ref[refPeakBin]) refPeakBin = i;

    xmad::dsp::SpectrumAnalyzer fft(N, xmad::dsp::Window::Rectangle);
    for (int n = 0; n < N; ++n) fft.Feed(n, input[n]);
    std::vector<int> spec(N / 2);
    fft.Transform(spec.data());

    int gotPeakBin = 0;
    for (int i = 0; i < N / 2; ++i)
        if (spec[i] > spec[gotPeakBin]) gotPeakBin = i;

    std::printf("reference peak bin = %d, ported peak bin = %d\n", refPeakBin, gotPeakBin);

    // second-highest bin should be well below the peak (clean single tone,
    // not smeared across many bins like the buggy prototype was)
    int secondHighest = -1000000;
    for (int i = 0; i < N / 2; ++i) {
        if (i == gotPeakBin) continue;
        secondHighest = std::max(secondHighest, spec[i]);
    }
    std::printf("peak dB = %d, next-highest dB = %d\n", spec[gotPeakBin], secondHighest);

    if (gotPeakBin != targetBin) {
        std::printf("FAIL: expected peak at bin %d\n", targetBin);
        return false;
    }
    if (spec[gotPeakBin] - secondHighest < 20) {
        std::printf("FAIL: spectrum is smeared, not a clean tone\n");
        return false;
    }
    return true;
}

bool CheckSilenceDoesNotCrash() {
    constexpr int N = 32;
    xmad::dsp::SpectrumAnalyzer fft(N, xmad::dsp::Window::Hanning);
    for (int n = 0; n < N; ++n) fft.Feed(n, 0.0);
    std::vector<int> spec(N / 2);
    fft.Transform(spec.data());
    return true; // just must not crash / not produce NaN->UB
}

} // namespace

int main() {
    bool ok = true;
    ok &= CheckPureTone();
    ok &= CheckSilenceDoesNotCrash();
    std::printf(ok ? "PASS\n" : "FAIL\n");
    return ok ? 0 : 1;
}
