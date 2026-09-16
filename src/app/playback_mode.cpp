#include "app/playback_mode.h"

namespace xmad::app {

void ResetShuffleBag(ShuffleState& state, size_t playlistSize, uint64_t generation) {
    state.played.assign(playlistSize, false);
    state.playlistGeneration = generation;
}

int NextAutoAdvanceIndex(int currentIndex, size_t playlistSize, bool repeatEnabled, bool randomEnabled,
                          ShuffleState& shuffleState, uint64_t playlistGeneration,
                          const std::function<size_t(size_t)>& nextRandom) {
    if (playlistSize == 0) return kStopPlayback;

    // Self-heals if the playlist mutated (Add/Clear/RemoveAt/.../LoadM3U)
    // since the bag was last built - Playlist::Generation() already
    // exists for exactly this kind of cheap change-detection.
    if (shuffleState.playlistGeneration != playlistGeneration || shuffleState.played.size() != playlistSize) {
        ResetShuffleBag(shuffleState, playlistSize, playlistGeneration);
    }

    if (randomEnabled) {
        if (currentIndex >= 0 && static_cast<size_t>(currentIndex) < playlistSize) {
            shuffleState.played[static_cast<size_t>(currentIndex)] = true;
        }

        std::vector<size_t> unplayed;
        for (size_t i = 0; i < playlistSize; ++i) {
            if (!shuffleState.played[i]) unplayed.push_back(i);
        }

        if (unplayed.empty()) {
            if (!repeatEnabled) return kStopPlayback;
            // Bag exhausted with Repeat also on: reshuffle a fresh bag
            // rather than the original's quirk of restarting sequentially
            // at track 0 (a deliberate, user-requested divergence).
            ResetShuffleBag(shuffleState, playlistSize, playlistGeneration);
            if (playlistSize > 1) {
                for (size_t i = 0; i < playlistSize; ++i) {
                    if (i != static_cast<size_t>(currentIndex)) unplayed.push_back(i);
                }
            } else {
                unplayed.push_back(0);
            }
        }

        const size_t pick = nextRandom(unplayed.size());
        const size_t chosen = unplayed[pick < unplayed.size() ? pick : 0];
        shuffleState.played[chosen] = true;
        return static_cast<int>(chosen);
    }

    const bool onLastTrack = static_cast<size_t>(currentIndex) + 1 >= playlistSize;
    if (repeatEnabled && onLastTrack) return 0;
    if (!onLastTrack) return currentIndex + 1;
    return kStopPlayback;
}

} // namespace xmad::app
