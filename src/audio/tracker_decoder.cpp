#include "audio/tracker_decoder.h"

#include <algorithm>
#include <stdexcept>
#include <vector>

#include "audio/module_file.h"

extern "C" {
#include "ibxm.h"
}

namespace xmad::audio {

namespace {

// Trackers have no native sample rate - ibxm renders at whatever it's
// asked for, and Engine opens the audio device at sampleRate().
constexpr int kSampleRate = 48000;
// Linear interpolation on. Off (0) is closer to the gritty, aliased sound of
// the original Amiga/DOS hardware; ibxm already oversamples 2x internally
// either way.
constexpr int kInterpolation = 1;
// Gzip bomb guard for ReadModuleFile - real modules are a few MB at most.
constexpr size_t kMaxModuleBytes = 64 * 1024 * 1024;

class TrackerDecoder : public Decoder {
public:
    explicit TrackerDecoder(const std::string& path) {
        std::vector<char> bytes = ReadModuleFile(path, kMaxModuleBytes);
        data d{bytes.data(), static_cast<int>(bytes.size())};
        char message[64] = {0};
        // module_load copies every sample into its own allocations, so
        // `bytes` can go out of scope as soon as this returns.
        module_ = module_load(&d, message);
        if (!module_) throw std::runtime_error("ibxm: " + std::string(message) + " (" + path + ")");
        replay_ = new_replay(module_, kSampleRate, kInterpolation);
        if (!replay_) {
            dispose_module(module_);
            throw std::runtime_error("ibxm: out of memory opening " + path);
        }
        // Song length = until the sequence first revisits a row it has
        // already played (ibxm tracks this per row, so pattern-jump loops
        // and "play forever" modules still end). Only steps through
        // rows/effects - no mixing - so it's fast, and it happens here,
        // before Engine starts the decode thread, for the same reason
        // Mp3Decoder scans here (see Decoder::totalFrames). Without a real
        // length, ReadFrames would never run short, Engine would never see
        // end-of-track, and the playlist would never advance.
        // replay_calculate_duration resets the replay to the start.
        totalFrames_ = static_cast<uint64_t>(std::max(0, replay_calculate_duration(replay_)));
        mixBuf_.resize(static_cast<size_t>(calculate_mix_buf_len(kSampleRate)));
    }

    ~TrackerDecoder() override {
        dispose_replay(replay_);
        dispose_module(module_);
    }

    uint64_t ReadFrames(float* out, uint64_t frameCount) override {
        uint64_t written = 0;
        while (written < frameCount && position_ < totalFrames_) {
            if (pendingPos_ == pendingLen_) {
                // One tick of audio per call (tempo-dependent length),
                // interleaved stereo ints at 16-bit scale, unclipped.
                pendingLen_ = static_cast<uint64_t>(replay_get_audio(replay_, mixBuf_.data(), 0));
                pendingPos_ = 0;
                if (pendingLen_ == 0) break;
            }
            const uint64_t n = std::min({frameCount - written, pendingLen_ - pendingPos_, totalFrames_ - position_});
            if (out) {
                const int* src = mixBuf_.data() + pendingPos_ * 2;
                float* dst = out + written * 2;
                for (uint64_t i = 0; i < n * 2; ++i) {
                    dst[i] = std::clamp(static_cast<float>(src[i]) / 32768.0f, -1.0f, 1.0f);
                }
            }
            pendingPos_ += n;
            position_ += n;
            written += n;
        }
        return written;
    }

    bool SeekToFrame(uint64_t frame) override {
        frame = std::min(frame, totalFrames_);
        // replay_seek restarts from the top and fast-forwards whole ticks
        // without mixing, landing on the last tick boundary at or before
        // `frame`; render (and throw away) the remainder so the position is
        // exact.
        position_ = static_cast<uint64_t>(std::max(0, replay_seek(replay_, static_cast<int>(frame))));
        pendingPos_ = pendingLen_ = 0;
        if (position_ < frame) ReadFrames(nullptr, frame - position_);
        return true;
    }

    uint32_t channels() const override { return 2; }
    uint32_t sampleRate() const override { return kSampleRate; }
    uint64_t totalFrames() const override { return totalFrames_; }

private:
    module* module_ = nullptr;
    replay* replay_ = nullptr;
    std::vector<int> mixBuf_;
    uint64_t pendingPos_ = 0, pendingLen_ = 0; // unread frames of the last tick in mixBuf_
    uint64_t position_ = 0;
    uint64_t totalFrames_ = 0; // computed once in the constructor - see its comment
};

} // namespace

std::unique_ptr<Decoder> OpenTrackerDecoder(const std::string& path) {
    return std::make_unique<TrackerDecoder>(path);
}

} // namespace xmad::audio
