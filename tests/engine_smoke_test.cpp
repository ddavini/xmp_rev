// Confirms the full pipeline moves: Engine::Open spins up the decode
// thread and SDL audio device, and playback position should advance in
// roughly real time if the device is actually consuming audio.

#include "audio/engine.h"

#include <SDL2/SDL.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <thread>

int main(int argc, char** argv) {
    const std::string path = argc >= 2 ? argv[1] : "tests/fixtures/tone.mp3";
    const std::string otherPath = argc >= 3 ? argv[2] : "tests/fixtures/tone_long.mp3";

    if (SDL_Init(SDL_INIT_AUDIO) != 0) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    int numDrivers = SDL_GetNumAudioDrivers();
    std::printf("SDL audio drivers available: %d (current: %s)\n", numDrivers,
                SDL_GetCurrentAudioDriver() ? SDL_GetCurrentAudioDriver() : "(none)");

    xmad::audio::Engine engine;
    if (!engine.Open(path)) {
        std::fprintf(stderr, "Engine::Open('%s') failed\n", path.c_str());
        return 1;
    }
    std::printf("opened %s: %u Hz, %u ch, duration=%.2fs\n", path.c_str(), engine.sampleRate(),
                engine.channels(), engine.durationSeconds());

    const double before = engine.positionSeconds();
    std::this_thread::sleep_for(std::chrono::milliseconds(800));
    const double after = engine.positionSeconds();
    std::printf("position before=%.3fs after=%.3fs (slept 0.8s)\n", before, after);

    if (after - before < 0.3) {
        std::printf("NOTE: position barely advanced - likely no real audio device available in this "
                    "environment (sandboxed/headless), not necessarily a code bug. Check "
                    "SDL_GetCurrentAudioDriver() above.\n");
        engine.Stop();
        SDL_Quit();
        return 2;
    }
    std::printf("PASS: position advanced in real time\n");

    // Regression check for "clear the playlist, load a new song, press
    // Play - it plays the old song": Stop() alone deliberately leaves the
    // decoder loaded (so Play can restart the same track), which is wrong
    // once the track has actually been cleared/replaced - the UI's Play
    // handler used channels() != 0 as "something is already open" and
    // resurrected the stale one. Close() must fully release it.
    engine.Close();
    if (engine.channels() != 0) {
        std::fprintf(stderr, "FAIL: engine.channels() still %u after Close()\n", engine.channels());
        SDL_Quit();
        return 1;
    }
    if (!engine.Open(otherPath)) {
        std::fprintf(stderr, "Engine::Open('%s') failed after Close()\n", otherPath.c_str());
        SDL_Quit();
        return 1;
    }
    std::printf("reopened '%s' after Close(): duration=%.2fs\n", otherPath.c_str(), engine.durationSeconds());
    if (std::fabs(engine.durationSeconds() - 1.0) < 0.3) {
        std::fprintf(stderr,
                      "FAIL: still reports the original ~1s track's duration after opening a different "
                      "file post-Close() - the old decoder wasn't actually released\n");
        SDL_Quit();
        return 1;
    }
    engine.Stop();
    SDL_Quit();
    std::printf("PASS: Close() fully released the old track; reopen picked up the new one\n");
    return 0;
}
