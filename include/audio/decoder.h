#pragma once

#include <cstdint>
#include <memory>
#include <string>

// Replaces the original xmMP3.dll: that engine's decode core (mp3dec, see
// Source/VC++/mp3dec/readme.txt) is Xing Technology's proprietary decoder,
// relicensed as GPL by a third party who didn't own it - not safe to carry
// forward. This wraps dr_mp3 / dr_flac instead (both David Reid,
// public domain / MIT-0, vendored in third_party/), which also gets FLAC
// support the original never had.

namespace xmad::audio {

class Decoder {
public:
    virtual ~Decoder() = default;

    // Reads up to `frameCount` frames (one sample per channel = one frame)
    // into `out` as interleaved 32-bit float in [-1, 1]. `out` must hold at
    // least frameCount * channels() floats. Returns frames actually written
    // (less than requested at end of stream).
    virtual uint64_t ReadFrames(float* out, uint64_t frameCount) = 0;

    virtual bool SeekToFrame(uint64_t frame) = 0;

    virtual uint32_t channels() const = 0;
    virtual uint32_t sampleRate() const = 0;
    // 0 if unknown. MUST be cheap and safe to call from any thread at any
    // time (Engine::durationSeconds(), called from the main thread every
    // render frame, wraps this) - implementations that need to derive this
    // by scanning/seeking the underlying file (dr_mp3, which has no
    // reliable frame-count header) must do that scan once, synchronously,
    // before any other thread can touch the decoder, and cache the result;
    // never on demand. Real crash, fixed once: an earlier MP3
    // implementation rescanned on every call, which - since it's called
    // every frame from the main thread while the decode thread
    // concurrently reads the same non-thread-safe drmp3 handle for
    // playback - corrupted shared decoder state and reliably SIGSEGV'd
    // (confirmed via a real user-submitted crash report, both threads
    // captured mid-crash inside drmp3dec_decode_frame simultaneously).
    virtual uint64_t totalFrames() const = 0;
};

// Picks a decoder by file extension (.mp3 / .flac). Throws std::runtime_error
// on an unrecognized extension or a decode failure.
std::unique_ptr<Decoder> OpenDecoder(const std::string& path);

} // namespace xmad::audio
