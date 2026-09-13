#include "audio/mp3_decoder.h"

#include <stdexcept>

#include "dr_mp3.h"

namespace xmad::audio {

namespace {

class Mp3Decoder : public Decoder {
public:
    explicit Mp3Decoder(const std::string& path) {
        if (!drmp3_init_file(&dec_, path.c_str(), nullptr)) {
            throw std::runtime_error("dr_mp3: failed to open " + path);
        }
        opened_ = true;
        // Scans the file once, here, to count frames (dr_mp3 doesn't index
        // this at open time - MP3 has no reliable frame-count header) and
        // caches it. Real crash, fixed once (see Decoder::totalFrames'
        // comment): totalFrames() used to call this scan on every
        // invocation instead of caching it, and it was being called every
        // render frame from the main thread (the seek bar's fraction,
        // Info/BitRate readouts) while the decode thread concurrently read
        // the same `dec_` for playback - drmp3_get_pcm_frame_count
        // actually seeks through the shared, non-thread-safe drmp3 handle,
        // so that was a genuine data race that corrupted decoder state and
        // crashed (confirmed via a real crash report: both threads caught
        // mid-crash inside drmp3dec_decode_frame at the same time). Doing
        // the scan here instead, before this decoder is ever handed to
        // Engine::Open (which only starts the decode thread afterward), is
        // strictly single-threaded - nothing else can be touching `dec_`
        // yet - so the same scan is now race-free by construction, not by
        // careful call-site discipline.
        totalFrames_ = drmp3_get_pcm_frame_count(&dec_);
    }

    ~Mp3Decoder() override {
        if (opened_) drmp3_uninit(&dec_);
    }

    uint64_t ReadFrames(float* out, uint64_t frameCount) override {
        return drmp3_read_pcm_frames_f32(&dec_, frameCount, out);
    }

    bool SeekToFrame(uint64_t frame) override {
        return drmp3_seek_to_pcm_frame(&dec_, frame) == DRMP3_TRUE;
    }

    uint32_t channels() const override { return dec_.channels; }
    uint32_t sampleRate() const override { return dec_.sampleRate; }

    uint64_t totalFrames() const override { return totalFrames_; }

private:
    drmp3 dec_{};
    bool opened_ = false;
    uint64_t totalFrames_ = 0; // computed once in the constructor - see its comment
};

} // namespace

std::unique_ptr<Decoder> OpenMp3Decoder(const std::string& path) {
    return std::make_unique<Mp3Decoder>(path);
}

} // namespace xmad::audio
