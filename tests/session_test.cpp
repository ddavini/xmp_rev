// Covers SerializeSettings/ParseSettings, the pure part of session
// persistence - SaveSettingsFile/LoadSettingsFile just add filesystem I/O
// around these (see app/session.h).

#include "app/session.h"

#include <array>
#include <cstdio>
#include <string>
#include <unordered_map>

using xmad::app::ParseEqPerSong;
using xmad::app::ParseSettings;
using xmad::app::SerializeEqPerSong;
using xmad::app::SerializeSettings;
using xmad::app::Settings;

namespace {
int g_failures = 0;

void Check(bool cond, const char* what) {
    if (!cond) {
        std::printf("FAIL: %s\n", what);
        ++g_failures;
    }
}
} // namespace

int main() {
    // Round-trip: whatever SerializeSettings writes, ParseSettings reads back exactly.
    {
        Settings s;
        s.specMode = 4;
        s.volumePercent = 80;
        s.wasPlaying = true;
        s.currentIndex = 2;
        s.positionSeconds = 123.456;
        s.xSound = true;
        s.eqPreset = 1;
        s.eqBands = {60, 40, 20, 0, -20, -20, 0, 20, 40, 60};
        s.visPanel = 2;
        s.perSongEq = true;
        const std::string text = SerializeSettings(s);

        Settings parsed;
        const bool ok = ParseSettings(text, parsed);
        Check(ok, "round-trip: parse succeeds");
        Check(parsed.specMode == 4, "round-trip: specMode");
        Check(parsed.volumePercent == 80, "round-trip: volumePercent");
        Check(parsed.wasPlaying == true, "round-trip: wasPlaying");
        Check(parsed.wasPaused == false, "round-trip: wasPaused");
        Check(parsed.currentIndex == 2, "round-trip: currentIndex");
        Check(parsed.positionSeconds == 123.456, "round-trip: positionSeconds");
        Check(parsed.xSound == true, "round-trip: xSound");
        Check(parsed.eqPreset == 1, "round-trip: eqPreset");
        Check(parsed.eqBands == s.eqBands, "round-trip: eqBands");
        Check(parsed.visPanel == 2, "round-trip: visPanel");
        Check(parsed.perSongEq == true, "round-trip: perSongEq");
    }

    // wasPaused round-trips independently of wasPlaying - the fix for
    // "pause then quit loses the position" depends on these two being
    // distinguishable rather than collapsed into one bool.
    {
        Settings s;
        s.wasPlaying = false;
        s.wasPaused = true;
        s.positionSeconds = 42.5;
        const std::string text = SerializeSettings(s);

        Settings parsed;
        Check(ParseSettings(text, parsed), "wasPaused round-trip: parse succeeds");
        Check(parsed.wasPlaying == false, "wasPaused round-trip: wasPlaying stays false");
        Check(parsed.wasPaused == true, "wasPaused round-trip: wasPaused");
        Check(parsed.positionSeconds == 42.5, "wasPaused round-trip: positionSeconds");
    }

    // Empty input: no settings file yet (first run) - fails cleanly, caller
    // keeps Settings' own defaults.
    {
        Settings parsed;
        Check(!ParseSettings("", parsed), "empty input fails");
    }

    // Unknown keys and malformed lines are skipped, not fatal - known keys
    // elsewhere in the same text still parse.
    {
        Settings parsed;
        const bool ok = ParseSettings("SPECMODE=1\nNOTAKEY=whatever\ngarbage line\nVOLUME=60\n", parsed);
        Check(ok, "mixed valid/invalid lines still succeeds");
        Check(parsed.specMode == 1, "mixed: known key before garbage still parsed");
        Check(parsed.volumePercent == 60, "mixed: known key after garbage still parsed");
    }

    // A key present with a non-numeric value doesn't crash and doesn't
    // clobber the field's default.
    {
        Settings parsed;
        const bool ok = ParseSettings("VOLUME=not-a-number\nINDEX=3\n", parsed);
        Check(ok, "non-numeric value still succeeds overall");
        Check(parsed.volumePercent == 25, "non-numeric value leaves default in place");
        Check(parsed.currentIndex == 3, "later valid key still parses");
    }

    // Missing keys keep Settings' defaults rather than zeroing them.
    {
        Settings parsed;
        ParseSettings("INDEX=5\n", parsed);
        Check(parsed.specMode == 0, "missing key keeps default (specMode)");
        Check(parsed.volumePercent == 25, "missing key keeps default (volumePercent)");
        Check(parsed.wasPlaying == false, "missing key keeps default (wasPlaying)");
        Check(parsed.wasPaused == false, "missing key keeps default (wasPaused)");
        Check(parsed.positionSeconds == 0.0, "missing key keeps default (positionSeconds)");
        Check(parsed.xSound == false, "missing key keeps default (xSound)");
        Check(parsed.eqPreset == -1, "missing key keeps default (eqPreset)");
        Check(parsed.eqBands == Settings{}.eqBands, "missing key keeps default (eqBands)");
        Check(parsed.visPanel == 0, "missing key keeps default (visPanel)");
        Check(parsed.perSongEq == false, "missing key keeps default (perSongEq)");
    }

    // EQBANDS tolerates fewer fields than expected (fills what's present,
    // leaves the rest at their defaults) - same tolerant spirit as the
    // rest of this parser, matching a settings file from a future version
    // adding an 11th band, or a truncated/corrupt line.
    {
        Settings parsed;
        const bool ok = ParseSettings("EQPRESET=2\nEQBANDS=10,20,30\n", parsed);
        Check(ok, "short EQBANDS still succeeds");
        Check(parsed.eqPreset == 2, "short EQBANDS: eqPreset still parsed");
        Check(parsed.eqBands[0] == 10 && parsed.eqBands[1] == 20 && parsed.eqBands[2] == 30,
              "short EQBANDS: present values parsed");
        Check(parsed.eqBands[3] == 0, "short EQBANDS: missing trailing values stay default");
    }

    // SerializeEqPerSong/ParseEqPerSong: round-trip with multiple tracks.
    {
        std::unordered_map<std::string, std::array<int, 10>> bands;
        bands["/music/track a.mp3"] = {60, 40, 20, 0, -20, -20, 0, 20, 40, 60};
        bands["/music/track_b.flac"] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
        const std::string text = SerializeEqPerSong(bands);

        std::unordered_map<std::string, std::array<int, 10>> parsed;
        const bool ok = ParseEqPerSong(text, parsed);
        Check(ok, "eq-per-song round-trip: parse succeeds");
        Check(parsed.size() == 2, "eq-per-song round-trip: both tracks present");
        Check(parsed.count("/music/track a.mp3") == 1 && parsed["/music/track a.mp3"] == bands["/music/track a.mp3"],
              "eq-per-song round-trip: track a bands");
        Check(parsed.count("/music/track_b.flac") == 1 &&
                  parsed["/music/track_b.flac"] == bands["/music/track_b.flac"],
              "eq-per-song round-trip: track b bands");
    }

    // Empty input fails cleanly, same as ParseSettings - no file yet on
    // first run with per-song EQ never used.
    {
        std::unordered_map<std::string, std::array<int, 10>> parsed;
        Check(!ParseEqPerSong("", parsed), "eq-per-song: empty input fails");
    }

    // A malformed bands value for one track is skipped, not fatal - other
    // tracks in the same file still parse (same tolerant spirit as
    // EQBANDS/ParseSettings).
    {
        std::unordered_map<std::string, std::array<int, 10>> parsed;
        const bool ok = ParseEqPerSong("/a.mp3=1,2,3,4,5,6,7,8,9,10\n/b.mp3=not,valid\n/c.mp3=5,5,5,5,5,5,5,5,5,5\n",
                                        parsed);
        Check(ok, "eq-per-song: mixed valid/invalid lines still succeeds");
        Check(parsed.count("/a.mp3") == 1, "eq-per-song: track before malformed line still parsed");
        Check(parsed.count("/b.mp3") == 0, "eq-per-song: malformed line skipped entirely");
        Check(parsed.count("/c.mp3") == 1, "eq-per-song: track after malformed line still parsed");
    }

    // Fewer than 10 bands fills what's present and leaves the rest at 0,
    // same tolerance as EQBANDS.
    {
        std::unordered_map<std::string, std::array<int, 10>> parsed;
        ParseEqPerSong("/short.mp3=10,20,30\n", parsed);
        Check(parsed["/short.mp3"][0] == 10 && parsed["/short.mp3"][1] == 20 && parsed["/short.mp3"][2] == 30,
              "eq-per-song: short bands - present values parsed");
        Check(parsed["/short.mp3"][3] == 0, "eq-per-song: short bands - missing trailing values default to 0");
    }

    if (g_failures == 0) {
        std::printf("PASS\n");
        return 0;
    }
    std::printf("%d FAILURE(S)\n", g_failures);
    return 1;
}
