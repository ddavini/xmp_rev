#pragma once

#include <complex>
#include <vector>

// Independent, from-scratch spectrum analyzer - NOT a port of either
// Source/VC++/xmFFT/xmFFT.cpp (an unused prototype DLL: original code by
// the app's author, but never wired into the shipped app, and confirmed
// buggy by tests/fft_smoke_test.cpp) or Source/VC++/xmMP30159/src/FFT.cpp
// (the FFT actually shipped in xmMP3.dll - "(c) Reliable Software, 1996",
// third-party licensed, not safe to reuse here).
//
// Matches the *behavior* of the original live analyzer: same four
// windowing options and the same 20*log10(|X|/sqrt(N)) log-magnitude
// scaling, so the visualizer feels the same, on a clean-room algorithm.

namespace xmad::dsp {

enum class Window { Rectangle, Hanning, Hamming, Blackman };

class SpectrumAnalyzer {
public:
    // points must be a power of two.
    SpectrumAnalyzer(int points, Window window);

    // Feed one time-domain sample into slot `pos` (0 <= pos < points).
    void Feed(int pos, double sample);

    // Runs the FFT over the current buffer and writes points/2 log-magnitude
    // bins into `spec` (caller-owned, must hold points/2 ints).
    void Transform(int* spec) const;

    int points() const { return points_; }

private:
    int points_;
    int logPoints_;
    std::vector<double> window_;
    std::vector<double> tape_;
    mutable std::vector<std::complex<double>> work_;
};

} // namespace xmad::dsp
