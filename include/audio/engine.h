#pragma once

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <SDL2/SDL.h>

#include "audio/chorus.h"
#include "audio/compressor.h"
#include "audio/decoder.h"
#include "audio/equalizer.h"
#include "audio/reverb.h"
#include "audio/ring_buffer.h"
#include "audio/saturation.h"
#include "audio/stereo_widen.h"

// The playback engine: SDL audio output pulling from a ring buffer that a
// decode thread keeps fed. Shape mirrors Source/modxmMP3Interface.bas's
// xmMP3_open/play/stop/pause/seek/getState - same seams, new backend.
//
// Simplification vs. the original: the SDL audio device is opened at the
// decoder's native sample rate/channel count, so switching to a file with a
// different rate reopens the device (a brief pop, no crossfade/resampler
// yet). Good enough for one-file-at-a-time playback; a real mixer is a
// later concern once there's a playlist to gapless-transition between.

namespace xmad::audio {

enum class PlayState { Stopped, Playing, Paused };

class Engine {
public:
    Engine();
    ~Engine();

    // Opens `path` (by extension: .mp3 / .flac) and starts playback
    // immediately. Returns false on failure (bad file, unsupported format).
    bool Open(const std::string& path);

    void Play();
    void Pause();
    void Stop();
    void SeekSeconds(double seconds);

    // Fully releases the currently-open track (mirrors mStopStream, not
    // just AzzeraPosizione - StopAll's other half): after this, channels()
    // is 0 again. Stop() alone rewinds/pauses but deliberately leaves the
    // decoder loaded (so pressing Play again restarts the same track, per
    // the original's Play_Click semantics); that's wrong for "the track
    // that was open no longer has anywhere to point" cases like clearing
    // the playlist or deleting the currently-playing entry - without this,
    // engine.channels() != 0 still holds and a later Play() resurrects the
    // stale track instead of picking up whatever's newly current.
    void Close();

    PlayState state() const { return state_.load(); }
    double positionSeconds() const;
    double durationSeconds() const;
    uint32_t sampleRate() const { return decoder_ ? decoder_->sampleRate() : 0; }
    uint32_t channels() const { return decoder_ ? decoder_->channels() : 0; }

    // value: -127..127, matching frmEQ.frm's vsGraphic sliders.
    void SetEqBand(int band, int value) { eq_.SetBandValue(band, value); }
    int EqBand(int band) const { return eq_.BandValue(band); }

    // 0.0 (silent) .. 1.0 (full). Applied in the real-time audio callback,
    // after the EQ and before the samples reach the device, so
    // GetVisSnapshot (captured from that same buffer) reflects it too -
    // muting genuinely flatlines the visualizer, matching a real "nothing
    // is audible" state rather than just attenuating the speakers.
    void SetVolume(float v) { volume_.store(std::clamp(v, 0.0f, 1.0f)); }
    float Volume() const { return volume_.load(); }

    // Independent of volume_: mirrors xmp.frm's Mute button, which stashes
    // the current volume and visually parks the slider at 0 rather than
    // overwriting it - toggling mute off here restores exactly the volume_
    // that was already set, with no separate "remembered" value needed.
    void SetMuted(bool m) { muted_.store(m); }
    bool IsMuted() const { return muted_.load(); }

    // Mirrors the original's XSound "Surround" toggle (see
    // audio/stereo_widen.h for what this substitutes and why). Applied in
    // the decode thread, after the EQ and before the ring buffer, same as
    // eq_ - a no-op on mono content.
    void SetXSound(bool on) { xSound_.store(on); }
    bool XSound() const { return xSound_.load(); }

    // The 4 fixed-parameter effects below (see their own headers for the
    // DSP and tunings). Each is a straight on/off toggle - no runtime
    // parameter adjustment - applied in the decode thread in the order
    // Compression -> Saturation -> Chorus -> Reverb, after the EQ and
    // before xSound (see DecodeThreadMain). Named with an explicit "On"
    // suffix rather than e.g. Reverb()/Chorus(), which would collide with
    // the DSP class names used for reverb_/chorus_ below.
    void SetCompressionOn(bool on) { compressionOn_.store(on); }
    bool CompressionOn() const { return compressionOn_.load(); }
    void SetSaturationOn(bool on) { saturationOn_.store(on); }
    bool SaturationOn() const { return saturationOn_.load(); }
    void SetChorusOn(bool on) { chorusOn_.store(on); }
    bool ChorusOn() const { return chorusOn_.load(); }
    void SetReverbOn(bool on) { reverbOn_.store(on); }
    bool ReverbOn() const { return reverbOn_.load(); }

    // Snapshot of the most recent audio actually sent to the output device
    // (captured in the real-time audio callback, so it reflects what's
    // playing *now*, not what the decode thread has buffered ~1s ahead of
    // playback). Used to feed the spectrum analyzer / peak meters. Returns
    // false if nothing has played yet or the snapshot is momentarily locked
    // by the audio thread (never blocks - just returns false; caller
    // should reuse its last frame in that case).
    static constexpr int kVisSnapshotFrames = 1024;
    bool GetVisSnapshot(std::vector<float>& outInterleaved, int& outChannels) const;

private:
    static void SDLCALL AudioCallback(void* userdata, uint8_t* stream, int len);
    void DecodeThreadMain();
    void CloseDevice();
    void CaptureVisSnapshot(const float* interleaved, int frames, int channels);

    std::unique_ptr<Decoder> decoder_;
    std::unique_ptr<RingBuffer> ring_;
    Equalizer eq_;
    Compressor compressor_;
    Chorus chorus_;
    Reverb reverb_;
    SDL_AudioDeviceID device_ = 0;

    // Default 0.25 matches Form_Load's fresh-install fallback
    // (GetINI(..., "VOLUME", "25")) - see volume.bas/FunzioniGlobali.bas.
    std::atomic<float> volume_{0.25f};
    std::atomic<bool> muted_{false};
    std::atomic<bool> xSound_{false};
    std::atomic<bool> compressionOn_{false};
    std::atomic<bool> saturationOn_{false};
    std::atomic<bool> chorusOn_{false};
    std::atomic<bool> reverbOn_{false};

    mutable std::mutex visMutex_;
    std::vector<float> visSnapshot_;
    int visChannels_ = 0;
    int visFrames_ = 0;

    std::atomic<PlayState> state_{PlayState::Stopped};
    std::atomic<uint64_t> framesConsumed_{0}; // frames actually sent to the audio device
    std::atomic<uint64_t> seekTargetFrame_{UINT64_MAX}; // UINT64_MAX = no pending seek

    // Set by the decode thread when the decoder itself has no more frames.
    // NOT the same moment as "playback finished": decoding runs far faster
    // than real-time, so for a short file the decode thread can drain the
    // entire file into ring_ in milliseconds while only a fraction of it
    // has actually reached the speakers. state_ only flips to Stopped once
    // the audio callback finds ring_ empty *and* this is set - i.e. once
    // everything that was decoded has actually been played.
    std::atomic<bool> decoderExhausted_{false};

    std::thread decodeThread_;
    std::atomic<bool> quitDecodeThread_{false};
    std::mutex decodeThreadWakeMutex_;
    std::condition_variable decodeThreadWake_;
};

} // namespace xmad::audio
