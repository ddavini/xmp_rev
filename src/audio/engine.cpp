#include "audio/engine.h"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <vector>

namespace xmad::audio {

namespace {
constexpr int kDecodeChunkFrames = 4096;
constexpr double kRingBufferSeconds = 1.0; // decode-ahead buffer depth
constexpr float kXSoundWidth = 1.8f;        // fixed width factor - see SetXSound's doc comment
} // namespace

Engine::Engine() {
    if (SDL_WasInit(SDL_INIT_AUDIO) == 0) {
        SDL_InitSubSystem(SDL_INIT_AUDIO);
    }
}

Engine::~Engine() {
    Stop();
    if (decodeThread_.joinable()) {
        quitDecodeThread_ = true;
        decodeThreadWake_.notify_all();
        if (ring_) ring_->Shutdown();
        decodeThread_.join();
    }
    CloseDevice();
}

void Engine::CloseDevice() {
    if (device_ != 0) {
        SDL_CloseAudioDevice(device_);
        device_ = 0;
    }
}

bool Engine::Open(const std::string& path) {
    Stop();
    if (decodeThread_.joinable()) {
        quitDecodeThread_ = true;
        decodeThreadWake_.notify_all();
        if (ring_) ring_->Shutdown();
        decodeThread_.join();
    }
    quitDecodeThread_ = false;

    try {
        decoder_ = OpenDecoder(path);
    } catch (const std::exception& e) {
        std::cerr << "Engine::Open failed: " << e.what() << "\n";
        decoder_.reset();
        return false;
    }

    CloseDevice();

    SDL_AudioSpec want{};
    want.freq = static_cast<int>(decoder_->sampleRate());
    want.format = AUDIO_F32SYS;
    want.channels = static_cast<Uint8>(decoder_->channels());
    want.samples = 1024;
    want.callback = &Engine::AudioCallback;
    want.userdata = this;

    SDL_AudioSpec have{};
    device_ = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
    if (device_ == 0) {
        std::cerr << "SDL_OpenAudioDevice failed: " << SDL_GetError() << "\n";
        decoder_.reset();
        return false;
    }

    const size_t ringCapacityFrames =
        static_cast<size_t>(kRingBufferSeconds * decoder_->sampleRate());
    ring_ = std::make_unique<RingBuffer>(ringCapacityFrames, static_cast<int>(decoder_->channels()));
    // Recomputes filter coefficients/state for the new rate; band gains
    // (user's EQ settings) are untouched, so they persist across tracks.
    eq_.Reset(decoder_->sampleRate(), static_cast<int>(decoder_->channels()));
    compressor_.Reset(decoder_->sampleRate(), static_cast<int>(decoder_->channels()));
    chorus_.Reset(decoder_->sampleRate(), static_cast<int>(decoder_->channels()));
    reverb_.Reset(decoder_->sampleRate(), static_cast<int>(decoder_->channels()));

    framesConsumed_ = 0;
    seekTargetFrame_ = UINT64_MAX;
    decoderExhausted_ = false;

    decodeThread_ = std::thread(&Engine::DecodeThreadMain, this);

    Play();
    return true;
}

void Engine::Play() {
    if (!decoder_ || device_ == 0) return;
    state_ = PlayState::Playing;
    SDL_PauseAudioDevice(device_, 0);
    decodeThreadWake_.notify_all();
}

void Engine::Pause() {
    if (device_ == 0) return;
    state_ = PlayState::Paused;
    SDL_PauseAudioDevice(device_, 1);
}

void Engine::Stop() {
    if (device_ != 0) SDL_PauseAudioDevice(device_, 1);
    state_ = PlayState::Stopped;
    framesConsumed_ = 0;
    if (ring_) ring_->Clear();
    if (decoder_) {
        // Routed through the decode thread's own pendingSeek handling
        // (same mechanism SeekSeconds uses) rather than calling
        // decoder_->SeekToFrame() directly here - the decoder isn't safe
        // to touch concurrently from a thread other than the one already
        // driving it, and the decode thread might be mid-ReadFrames().
        // This also clears decoderExhausted_ for the next Play().
        seekTargetFrame_ = 0;
        decodeThreadWake_.notify_all();
    }
}

void Engine::Close() {
    Stop();
    if (decodeThread_.joinable()) {
        quitDecodeThread_ = true;
        decodeThreadWake_.notify_all();
        if (ring_) ring_->Shutdown();
        decodeThread_.join();
    }
    quitDecodeThread_ = false;
    CloseDevice();
    decoder_.reset();
    ring_.reset();
}

void Engine::RequestFade(float target, double seconds) {
    fadeRequestTarget_.store(target);
    fadeRequestSeconds_.store(seconds);
    fadePending_.store(true);
}

void Engine::SeekSeconds(double seconds) {
    if (!decoder_) return;
    const uint64_t frame = static_cast<uint64_t>(std::max(0.0, seconds) * decoder_->sampleRate());
    seekTargetFrame_ = frame;
    decodeThreadWake_.notify_all();
}

double Engine::positionSeconds() const {
    if (!decoder_ || decoder_->sampleRate() == 0) return 0.0;
    return static_cast<double>(framesConsumed_.load()) / decoder_->sampleRate();
}

double Engine::durationSeconds() const {
    if (!decoder_ || decoder_->sampleRate() == 0) return 0.0;
    return static_cast<double>(decoder_->totalFrames()) / decoder_->sampleRate();
}

void Engine::DecodeThreadMain() {
    const int channels = static_cast<int>(decoder_->channels());
    std::vector<float> chunk(static_cast<size_t>(kDecodeChunkFrames) * channels);

    while (!quitDecodeThread_) {
        // Checked every iteration regardless of play state so a seek always
        // takes effect immediately - including a seek issued right after
        // natural end-of-stream (e.g. Play() restarting the track).
        const uint64_t pendingSeek = seekTargetFrame_.exchange(UINT64_MAX);
        if (pendingSeek != UINT64_MAX) {
            decoder_->SeekToFrame(pendingSeek);
            ring_->Clear();
            framesConsumed_ = pendingSeek;
            decoderExhausted_ = false;
        }

        if (state_.load() != PlayState::Playing || decoderExhausted_.load()) {
            std::unique_lock<std::mutex> lock(decodeThreadWakeMutex_);
            decodeThreadWake_.wait_for(lock, std::chrono::milliseconds(100));
            continue;
        }

        const uint64_t got = decoder_->ReadFrames(chunk.data(), kDecodeChunkFrames);
        if (got == 0) {
            // The decoder has nothing left, but ring_ may still hold
            // buffered audio the speakers haven't reached yet - AudioCallback
            // is what actually flips state_ to Stopped, once that drains too.
            decoderExhausted_ = true;
            continue;
        }
        eq_.Process(chunk.data(), got);
        if (compressionOn_.load()) compressor_.Process(chunk.data(), got);
        if (saturationOn_.load()) ApplySaturation(chunk.data(), got, channels);
        if (chorusOn_.load()) chorus_.Process(chunk.data(), got);
        if (reverbOn_.load()) reverb_.Process(chunk.data(), got);
        if (xSound_.load()) ApplyStereoWiden(chunk.data(), got, channels, kXSoundWidth);
        ring_->Push(chunk.data(), got);
    }
}

void SDLCALL Engine::AudioCallback(void* userdata, uint8_t* stream, int len) {
    auto* self = static_cast<Engine*>(userdata);
    float* out = reinterpret_cast<float*>(stream);
    const int channels = static_cast<int>(self->channels());
    if (channels == 0) {
        std::fill(stream, stream + len, 0);
        return;
    }
    const size_t framesWanted = static_cast<size_t>(len) / sizeof(float) / channels;

    const size_t framesGot = self->ring_ ? self->ring_->TryPop(out, framesWanted) : 0;
    self->framesConsumed_ += framesGot;

    if (framesGot > 0) {
        if (self->fadePending_.exchange(false)) {
            const float target = self->fadeRequestTarget_.load();
            const double seconds = self->fadeRequestSeconds_.load();
            const uint64_t frames =
                std::max<uint64_t>(1, static_cast<uint64_t>(seconds * self->sampleRate()));
            self->fadeStep_ = (target - self->fadeGain_) / static_cast<float>(frames);
            self->fadeFramesRemaining_ = frames;
        }

        const float vol = self->muted_.load() ? 0.0f : self->volume_.load();
        if (vol != 1.0f || self->fadeGain_ != 1.0f || self->fadeFramesRemaining_ > 0) {
            for (size_t f = 0; f < framesGot; ++f) {
                if (self->fadeFramesRemaining_ > 0) {
                    self->fadeGain_ += self->fadeStep_;
                    if (--self->fadeFramesRemaining_ == 0) {
                        self->fadeGain_ = self->fadeRequestTarget_.load();
                    }
                }
                const float gain = vol * self->fadeGain_;
                float* frame = out + f * static_cast<size_t>(channels);
                for (int c = 0; c < channels; ++c) frame[c] *= gain;
            }
        }
        self->CaptureVisSnapshot(out, static_cast<int>(framesGot), channels);
    }

    if (framesGot < framesWanted && self->decoderExhausted_.load() &&
        self->state_.load() == PlayState::Playing) {
        // Nothing left buffered and the decoder has nothing more to give:
        // playback has genuinely finished (not just a transient underrun,
        // which wouldn't have decoderExhausted_ set). Doesn't touch
        // decoder_ itself (that's the decode thread's job, not this
        // real-time thread's) - callers are expected to SeekSeconds(0)
        // before the next Play(), same as handleTransportPress does.
        self->state_ = PlayState::Stopped;
        self->framesConsumed_ = 0;
    }

    if (framesGot < framesWanted) {
        // Underrun (or paused/stopped and nothing queued): silence the rest.
        std::fill(out + framesGot * channels, out + framesWanted * channels, 0.0f);
    }
}

void Engine::CaptureVisSnapshot(const float* interleaved, int frames, int channels) {
    std::unique_lock<std::mutex> lock(visMutex_, std::try_to_lock);
    if (!lock.owns_lock()) return; // never block the real-time audio thread
    const int n = std::min(frames, kVisSnapshotFrames);
    visSnapshot_.assign(interleaved, interleaved + static_cast<size_t>(n) * static_cast<size_t>(channels));
    visChannels_ = channels;
    visFrames_ = n;
}

bool Engine::GetVisSnapshot(std::vector<float>& outInterleaved, int& outChannels) const {
    std::unique_lock<std::mutex> lock(visMutex_, std::try_to_lock);
    if (!lock.owns_lock() || visFrames_ == 0) return false;
    outInterleaved = visSnapshot_;
    outChannels = visChannels_;
    return true;
}

} // namespace xmad::audio
