// Decodes real-encoded MP3 and FLAC fixtures (a synthetic 880Hz tone, see
// tests/fixtures/) and confirms the decoded PCM actually contains that
// tone, using the already-verified SpectrumAnalyzer rather than eyeballing
// sample counts. "It didn't throw" is not proof a decoder works.

#include "audio/decoder.h"
#include "dsp/fft.h"

#include <cmath>
#include <cstdio>
#include <vector>

namespace {

bool CheckFileDecodesToTone(const std::string& path, double expectedFreq) {
    std::printf("--- %s ---\n", path.c_str());
    auto dec = xmad::audio::OpenDecoder(path);

    const uint32_t channels = dec->channels();
    const uint32_t sampleRate = dec->sampleRate();
    std::printf("channels=%u sampleRate=%u totalFrames=%llu\n", channels, sampleRate,
                static_cast<unsigned long long>(dec->totalFrames()));

    // Read the whole file, downmix to mono.
    std::vector<float> mono;
    std::vector<float> chunk(channels * 4096);
    for (;;) {
        const uint64_t got = dec->ReadFrames(chunk.data(), 4096);
        if (got == 0) break;
        for (uint64_t i = 0; i < got; ++i) {
            float sum = 0;
            for (uint32_t c = 0; c < channels; ++c) sum += chunk[i * channels + c];
            mono.push_back(sum / static_cast<float>(channels));
        }
    }
    std::printf("decoded %zu mono frames\n", mono.size());
    if (mono.size() < 4096) {
        std::printf("FAIL: too few frames decoded\n");
        return false;
    }

    // Analyze a 4096-sample window with our own verified FFT.
    constexpr int N = 4096;
    xmad::dsp::SpectrumAnalyzer fft(N, xmad::dsp::Window::Hanning);
    for (int i = 0; i < N; ++i) fft.Feed(i, mono[i]);
    std::vector<int> spec(N / 2);
    fft.Transform(spec.data());

    int peakBin = 0;
    for (int i = 1; i < N / 2; ++i)
        if (spec[i] > spec[peakBin]) peakBin = i;

    const double binHz = static_cast<double>(sampleRate) / N;
    const double peakFreq = peakBin * binHz;
    std::printf("peak bin=%d (%.1f Hz), expected ~%.1f Hz, peak dB=%d\n", peakBin, peakFreq, expectedFreq,
                spec[peakBin]);

    if (std::abs(peakFreq - expectedFreq) > binHz * 2) {
        std::printf("FAIL: decoded tone frequency off by more than 2 bins\n");
        return false;
    }
    std::printf("PASS\n");
    return true;
}

} // namespace

int main() {
    bool ok = true;
    ok &= CheckFileDecodesToTone("tests/fixtures/tone.mp3", 880.0);
    ok &= CheckFileDecodesToTone("tests/fixtures/tone.flac", 880.0);
    std::printf(ok ? "ALL PASS\n" : "SOME FAILED\n");
    return ok ? 0 : 1;
}
