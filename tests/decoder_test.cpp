// Decodes real-encoded MP3 and FLAC fixtures (a synthetic 880Hz tone, see
// tests/fixtures/) and confirms the decoded PCM actually contains that
// tone, using the already-verified SpectrumAnalyzer rather than eyeballing
// sample counts. "It didn't throw" is not proof a decoder works. Same for
// the tracker-module fixtures (tests/fixtures/make_tracker_fixtures.py),
// plus their song length and seeking, which - unlike MP3/FLAC - the
// decoder computes itself by simulating playback.

#include "audio/decoder.h"
#include "dsp/fft.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
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

std::vector<float> ReadAll(xmad::audio::Decoder& dec) {
    std::vector<float> all;
    std::vector<float> chunk(dec.channels() * 4096);
    for (;;) {
        const uint64_t got = dec.ReadFrames(chunk.data(), 4096);
        if (got == 0) break;
        all.insert(all.end(), chunk.begin(), chunk.begin() + static_cast<long>(got * dec.channels()));
    }
    return all;
}

// Frames [frame, frame + count) after SeekToFrame(frame), compared against
// the same span of a straight read from the top. `skip` frames right after
// the seek point are excluded: ibxm crossfades the first 64 samples of each
// tick from the previous tick's tail, and a seek restarts that crossfade
// from silence.
bool SeekMatches(xmad::audio::Decoder& dec, const std::vector<float>& straight, uint64_t frame, uint64_t skip,
                 uint64_t count) {
    if (!dec.SeekToFrame(frame)) {
        std::printf("FAIL: SeekToFrame(%llu) returned false\n", static_cast<unsigned long long>(frame));
        return false;
    }
    std::vector<float> got((skip + count) * 2);
    const uint64_t n = dec.ReadFrames(got.data(), skip + count);
    if (n != skip + count) {
        std::printf("FAIL: after seek to %llu, read %llu of %llu frames\n", static_cast<unsigned long long>(frame),
                    static_cast<unsigned long long>(n), static_cast<unsigned long long>(skip + count));
        return false;
    }
    float maxDiff = 0;
    for (uint64_t i = skip * 2; i < (skip + count) * 2; ++i)
        maxDiff = std::max(maxDiff, std::fabs(got[i] - straight[frame * 2 + i]));
    std::printf("seek to %llu: max diff vs straight read %.6f\n", static_cast<unsigned long long>(frame), maxDiff);
    if (maxDiff > 1e-6f) {
        std::printf("FAIL: audio after seek doesn't match a straight read\n");
        return false;
    }
    return true;
}

// Every fixture is the same 64-row song (see make_tracker_fixtures.py):
// 64 rows * 6 ticks * 960 frames at 48 kHz.
bool CheckTrackerLengthAndSeek(const std::string& path) {
    constexpr uint64_t kExpectedFrames = 64 * 6 * 960;
    constexpr uint64_t kTick = 960;
    std::printf("--- %s (length/seek) ---\n", path.c_str());
    auto dec = xmad::audio::OpenDecoder(path);
    if (dec->totalFrames() != kExpectedFrames) {
        std::printf("FAIL: totalFrames=%llu, expected %llu\n", static_cast<unsigned long long>(dec->totalFrames()),
                    static_cast<unsigned long long>(kExpectedFrames));
        return false;
    }
    // Must actually stop there - Engine only detects end-of-track (and the
    // playlist only advances) when ReadFrames runs dry.
    const std::vector<float> straight = ReadAll(*dec);
    if (straight.size() != kExpectedFrames * 2) {
        std::printf("FAIL: read %zu frames to end of stream, expected %llu\n", straight.size() / 2,
                    static_cast<unsigned long long>(kExpectedFrames));
        return false;
    }
    // Backward to the very start: bit-exact from frame 0 (a fresh start has
    // no previous tick to crossfade from either).
    if (!SeekMatches(*dec, straight, 0, 0, 8192)) return false;
    // Forward, mid-tick: exact after the first tick boundary past the seek.
    if (!SeekMatches(*dec, straight, 200000 + 123, kTick, 8192)) return false;
    // Backward again, then check the stream still ends in the right place.
    if (!SeekMatches(*dec, straight, 100000, kTick, 4096)) return false;
    dec->SeekToFrame(kExpectedFrames - 500);
    std::vector<float> tail(2 * 4096);
    const uint64_t n = dec->ReadFrames(tail.data(), 4096);
    if (n != 500 || dec->ReadFrames(tail.data(), 4096) != 0) {
        std::printf("FAIL: seek near end then read gave %llu frames, expected exactly 500 then 0\n",
                    static_cast<unsigned long long>(n));
        return false;
    }
    std::printf("PASS\n");
    return true;
}

// A zip entry using a method we can't decompress (6 = PKZIP 1.x
// "implode") must fail loudly at open, not be handed to ibxm as garbage -
// which would "load" as a silent, broken MOD. Made by patching the method
// field of the stored fixture's entry in both headers.
bool CheckUnsupportedZipMethodRejected() {
    std::printf("--- zip with unsupported compression method ---\n");
    std::ifstream in("tests/fixtures/tone.s3z", std::ios::binary);
    std::string zip((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const size_t central = zip.find("PK\x01\x02");
    if (zip.size() < 30 || central == std::string::npos) {
        std::printf("FAIL: fixture tone.s3z isn't the expected single-entry zip\n");
        return false;
    }
    zip[8] = 6;           // local file header: compression method
    zip[central + 10] = 6; // central directory entry: compression method
    const std::string path = "build/unsupported_method.s3z";
    std::ofstream(path, std::ios::binary) << zip;
    try {
        xmad::audio::OpenDecoder(path);
    } catch (const std::exception& e) {
        const bool clear = std::string(e.what()).find("unsupported zip compression method 6") != std::string::npos;
        std::printf("threw: %s\n%s\n", e.what(), clear ? "PASS" : "FAIL: error doesn't name the problem");
        return clear;
    }
    std::printf("FAIL: opened without error\n");
    return false;
}

} // namespace

int main() {
    bool ok = true;
    ok &= CheckFileDecodesToTone("tests/fixtures/tone.mp3", 880.0);
    ok &= CheckFileDecodesToTone("tests/fixtures/tone.flac", 880.0);
    // Middle C of a 32-sample loop holding 4 sine cycles: base rate / 8 -
    // 8287 Hz for a 4-channel MOD, 8363 Hz for XM/S3M.
    ok &= CheckFileDecodesToTone("tests/fixtures/tone.mod", 8287.0 / 8);
    ok &= CheckFileDecodesToTone("tests/fixtures/tone.mdz", 8287.0 / 8);
    ok &= CheckFileDecodesToTone("tests/fixtures/tone.xm", 8363.0 / 8);
    ok &= CheckFileDecodesToTone("tests/fixtures/tone.xmz", 8363.0 / 8);
    ok &= CheckFileDecodesToTone("tests/fixtures/tone.s3m", 8363.0 / 8);
    ok &= CheckFileDecodesToTone("tests/fixtures/tone.s3z", 8363.0 / 8);      // zip, stored
    ok &= CheckFileDecodesToTone("tests/fixtures/tone_gzip.mdz", 8287.0 / 8); // gzip, not zip
    for (const char* f : {"tests/fixtures/tone.mod", "tests/fixtures/tone.mdz", "tests/fixtures/tone.xm",
                          "tests/fixtures/tone.s3m"})
        ok &= CheckTrackerLengthAndSeek(f);
    ok &= CheckUnsupportedZipMethodRejected();
    std::printf(ok ? "ALL PASS\n" : "SOME FAILED\n");
    return ok ? 0 : 1;
}
