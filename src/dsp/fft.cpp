#include "dsp/fft.h"

#include <cmath>
#include <stdexcept>

namespace xmad::dsp {

namespace {

int Log2(int points) {
    int bits = 0;
    for (int p = points; p > 1; p >>= 1) ++bits;
    return bits;
}

unsigned int ReverseBits(unsigned int value, int numBits) {
    unsigned int reversed = 0;
    for (int i = 0; i < numBits; ++i) {
        reversed = (reversed << 1) | (value & 1u);
        value >>= 1;
    }
    return reversed;
}

} // namespace

SpectrumAnalyzer::SpectrumAnalyzer(int points, Window window)
    : points_(points), logPoints_(Log2(points)), tape_(points, 0.0), work_(points) {
    if (points < 2 || (points & (points - 1)) != 0) {
        throw std::invalid_argument("SpectrumAnalyzer: points must be a power of two");
    }

    window_.resize(points);
    const double twoPi = 2.0 * M_PI;
    switch (window) {
        case Window::Hanning:
            for (int i = 0; i < points; ++i)
                window_[i] = 0.5 - 0.5 * std::cos(twoPi * i / points);
            break;
        case Window::Hamming:
            for (int i = 0; i < points; ++i)
                window_[i] = 0.54 - 0.46 * std::cos(twoPi * i / points);
            break;
        case Window::Blackman:
            for (int i = 0; i < points; ++i)
                window_[i] = 0.42 - 0.5 * std::cos(twoPi * i / points) +
                             0.08 * std::cos(2.0 * twoPi * i / points);
            break;
        case Window::Rectangle:
        default:
            std::fill(window_.begin(), window_.end(), 1.0);
            break;
    }
}

void SpectrumAnalyzer::Feed(int pos, double sample) { tape_[pos] = sample * window_[pos]; }

void SpectrumAnalyzer::Transform(int* spec) const {
    // Bit-reversal permutation into the scratch buffer.
    for (int i = 0; i < points_; ++i) {
        work_[ReverseBits(static_cast<unsigned int>(i), logPoints_)] = std::complex<double>(tape_[i], 0.0);
    }

    // Iterative radix-2 Cooley-Tukey (decimation in time).
    for (int stageSize = 2; stageSize <= points_; stageSize <<= 1) {
        const int half = stageSize / 2;
        const double angleStep = -2.0 * M_PI / stageSize;
        for (int start = 0; start < points_; start += stageSize) {
            for (int k = 0; k < half; ++k) {
                const std::complex<double> twiddle = std::polar(1.0, angleStep * k);
                const std::complex<double> even = work_[start + k];
                const std::complex<double> odd = twiddle * work_[start + k + half];
                work_[start + k] = even + odd;
                work_[start + k + half] = even - odd;
            }
        }
    }

    const double sqrtPoints = std::sqrt(static_cast<double>(points_));
    for (int i = 0; i < points_ / 2; ++i) {
        const double magnitude = std::abs(work_[i]) / sqrtPoints;
        // Matches the shipped analyzer's dB-style scale; floor avoids -inf
        // on digital silence.
        spec[i] = static_cast<int>(20.0 * std::log10(magnitude > 1e-9 ? magnitude : 1e-9));
    }
}

} // namespace xmad::dsp
