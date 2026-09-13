#include "audio/flac_decoder.h"

#include <stdexcept>

#include "dr_flac.h"

namespace xmad::audio {

namespace {

class FlacDecoder : public Decoder {
public:
    explicit FlacDecoder(const std::string& path) {
        dec_ = drflac_open_file(path.c_str(), nullptr);
        if (!dec_) throw std::runtime_error("dr_flac: failed to open " + path);
    }

    ~FlacDecoder() override {
        if (dec_) drflac_close(dec_);
    }

    uint64_t ReadFrames(float* out, uint64_t frameCount) override {
        return drflac_read_pcm_frames_f32(dec_, frameCount, out);
    }

    bool SeekToFrame(uint64_t frame) override {
        return drflac_seek_to_pcm_frame(dec_, frame) == DRFLAC_TRUE;
    }

    uint32_t channels() const override { return dec_->channels; }
    uint32_t sampleRate() const override { return dec_->sampleRate; }
    uint64_t totalFrames() const override { return dec_->totalPCMFrameCount; }

private:
    drflac* dec_ = nullptr;
};

} // namespace

std::unique_ptr<Decoder> OpenFlacDecoder(const std::string& path) {
    return std::make_unique<FlacDecoder>(path);
}

} // namespace xmad::audio
