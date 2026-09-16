#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

// Pure decision logic for what to play next when a track finishes
// naturally - mirrors FunzioniGlobali.bas's PlayDone exactly (Random
// first, then Repeat-at-last-track, then sequential advance, then stop),
// with one deliberate divergence: when Random's "already played this
// cycle" bag is exhausted and Repeat is also on, this reshuffles a fresh
// bag instead of the original's quirk of restarting sequentially at
// track 0. No SDL/engine/playlist dependency, so it's directly
// unit-testable (see tests/playback_mode_test.cpp) - same split as
// window_snap.h/session.h's pure functions from their I/O shells.
// Randomness is injected (nextRandom) rather than calling std::rand
// internally, so tests can assert exact sequences.

namespace xmad::app {

// Returned by NextAutoAdvanceIndex to mean "stop, do not advance" -
// mirrors PlayDone's else-branch (g_PlayDone = False, "Play Done.", no
// further PlayStream call).
inline constexpr int kStopPlayback = -1;

// "Already played this Random cycle" bag. Lives outside Playlist itself
// (transient, UI-adjacent state, not a Playlist invariant) - a plain
// local variable in main.cpp, reset automatically by
// NextAutoAdvanceIndex whenever playlistGeneration no longer matches
// (the playlist mutated since the bag was built) or the bag empties out.
struct ShuffleState {
    std::vector<bool> played;
    uint64_t playlistGeneration = 0;
};

// Resets `state` to "nothing played yet" for a playlist of `playlistSize`
// tracks, tagging it with `generation` so future calls can detect
// staleness cheaply via Playlist::Generation().
void ResetShuffleBag(ShuffleState& state, size_t playlistSize, uint64_t generation);

// The exact 4-case PlayDone priority order (Random > Repeat > sequential
// > stop), with the reshuffle-on-exhaustion divergence noted above.
// nextRandom(n) must return a value in [0, n) and is only ever called
// with the current *unplayed* count, not the full playlist size.
int NextAutoAdvanceIndex(int currentIndex, size_t playlistSize, bool repeatEnabled,
                          bool randomEnabled, ShuffleState& shuffleState, uint64_t playlistGeneration,
                          const std::function<size_t(size_t)>& nextRandom);

} // namespace xmad::app
