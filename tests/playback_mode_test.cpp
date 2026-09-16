// Covers NextAutoAdvanceIndex's exact PlayDone-mirroring priority order
// (Random > Repeat > sequential > stop), including the concrete
// regression check that the old "always wrap forever" placeholder is
// gone (case 2) and the deliberate reshuffle-on-exhaustion divergence
// from the original's restart-at-0 quirk (case 6).

#include "app/playback_mode.h"

#include <cstdio>
#include <set>

using xmad::app::kStopPlayback;
using xmad::app::NextAutoAdvanceIndex;
using xmad::app::ResetShuffleBag;
using xmad::app::ShuffleState;

namespace {
int g_failures = 0;

void Check(bool cond, const char* what) {
    if (!cond) {
        std::printf("FAIL: %s\n", what);
        ++g_failures;
    }
}

// Always picks the first candidate - deterministic, so multi-step
// sequences can be asserted exactly.
size_t FirstOf(size_t n) { return n > 0 ? 0 : 0; }
} // namespace

int main() {
    // 1. Neither on, not last track -> current+1.
    {
        ShuffleState st;
        const int next = NextAutoAdvanceIndex(2, 5, false, false, st, 1, FirstOf);
        Check(next == 3, "neither on, mid-playlist: advances by 1");
    }

    // 2. Neither on, on last track -> kStopPlayback. The concrete
    // regression check that the old always-wrap placeholder is gone.
    {
        ShuffleState st;
        const int next = NextAutoAdvanceIndex(4, 5, false, false, st, 1, FirstOf);
        Check(next == kStopPlayback, "neither on, last track: stops instead of wrapping");
    }

    // 3. Repeat-only, on last track -> 0.
    {
        ShuffleState st;
        const int next = NextAutoAdvanceIndex(4, 5, true, false, st, 1, FirstOf);
        Check(next == 0, "repeat-only, last track: restarts at 0");
    }

    // 4. Repeat-only, not last track -> unaffected, current+1.
    {
        ShuffleState st;
        const int next = NextAutoAdvanceIndex(1, 5, true, false, st, 1, FirstOf);
        Check(next == 2, "repeat-only, mid-playlist: still just advances by 1");
    }

    // 5. Random-only: exhausts the bag then stops. A 3-track playlist
    // starting at index 0 needs only 2 more calls to visit the other 2
    // tracks (the starting track counts as already-played), then the
    // 3rd call finds nothing left and stops.
    {
        ShuffleState st;
        std::set<int> visited;
        int current = 0;
        for (int i = 0; i < 2; ++i) {
            const int next = NextAutoAdvanceIndex(current, 3, false, true, st, 1, FirstOf);
            Check(next != kStopPlayback, "random-only: does not stop before the bag is exhausted");
            visited.insert(next);
            current = next;
        }
        Check(visited.size() == 2, "random-only: both remaining indices visited exactly once");
        const int afterExhausted = NextAutoAdvanceIndex(current, 3, false, true, st, 1, FirstOf);
        Check(afterExhausted == kStopPlayback, "random-only: stops once the bag is exhausted");
    }

    // 6. Random+Repeat: after exhaustion, reshuffles a fresh bag instead
    // of the original's restart-at-0 quirk. Same 2-calls-to-exhaust
    // reasoning as case 5; the 3rd call is the reshuffle event itself.
    {
        ShuffleState st;
        int current = 0;
        for (int i = 0; i < 2; ++i) {
            current = NextAutoAdvanceIndex(current, 3, true, true, st, 1, FirstOf);
            Check(current != kStopPlayback, "random+repeat: normal picks before exhaustion never stop");
        }
        // Bag of {0,1,2} is now exhausted; FirstOf always returns index 0
        // of whatever candidate list it's given, so a silent restart-at-0
        // quirk would return 0 here too - the real proof is in the *bag
        // state*: after reshuffling, exactly one index (the just-picked
        // one) is marked played, not all-but-one as a stale bag would show.
        const int afterExhaustion = NextAutoAdvanceIndex(current, 3, true, true, st, 1, FirstOf);
        Check(afterExhaustion != kStopPlayback, "random+repeat: never stops on exhaustion");
        int playedCount = 0;
        for (bool p : st.played) {
            if (p) ++playedCount;
        }
        Check(playedCount == 1, "random+repeat: bag was reshuffled fresh (one index marked), not left stale");
    }

    // 7. Playlist size 1, all 4 flag combinations.
    {
        ShuffleState st;
        Check(NextAutoAdvanceIndex(0, 1, false, false, st, 1, FirstOf) == kStopPlayback,
              "size-1 playlist, neither on: stops (already on the 'last' track)");
    }
    {
        ShuffleState st;
        Check(NextAutoAdvanceIndex(0, 1, true, false, st, 1, FirstOf) == 0,
              "size-1 playlist, repeat-only: restarts the same track");
    }
    {
        ShuffleState st;
        Check(NextAutoAdvanceIndex(0, 1, false, true, st, 1, FirstOf) == kStopPlayback,
              "size-1 playlist, random-only: stops (nothing else to shuffle to)");
    }
    {
        ShuffleState st;
        Check(NextAutoAdvanceIndex(0, 1, true, true, st, 1, FirstOf) == 0,
              "size-1 playlist, random+repeat: collapses to replaying the only track");
    }

    // 8. Empty playlist -> kStopPlayback regardless of flags.
    {
        ShuffleState st;
        Check(NextAutoAdvanceIndex(0, 0, false, false, st, 1, FirstOf) == kStopPlayback,
              "empty playlist, neither on: stops");
        Check(NextAutoAdvanceIndex(0, 0, true, true, st, 1, FirstOf) == kStopPlayback,
              "empty playlist, random+repeat: still stops");
    }

    // 9. playlistGeneration mismatch mid-cycle transparently resets the
    // bag rather than reading stale/out-of-range state.
    {
        ShuffleState st;
        NextAutoAdvanceIndex(0, 3, false, true, st, 1, FirstOf); // marks index 0 played, generation=1
        Check(st.played[0], "generation test: index 0 marked played before the mutation");
        // Simulate a playlist mutation (Add/RemoveAt/... bumps Generation()).
        const int next = NextAutoAdvanceIndex(0, 3, false, true, st, 2, FirstOf);
        Check(next != kStopPlayback, "generation mismatch: bag transparently resets instead of misreading stale state");
        Check(st.playlistGeneration == 2, "generation mismatch: bag re-tagged with the new generation");
    }

    if (g_failures == 0) {
        std::printf("PASS\n");
        return 0;
    }
    std::printf("%d FAILURE(S)\n", g_failures);
    return 1;
}
