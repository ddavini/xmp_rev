// Covers SerializeSettings/ParseSettings, the pure part of session
// persistence - SaveSettingsFile/LoadSettingsFile just add filesystem I/O
// around these (see app/session.h).

#include "app/session.h"

#include <cstdio>

using xmad::app::ParseSettings;
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
        s.xSound = true;
        s.eqPreset = 1;
        s.eqBands = {60, 40, 20, 0, -20, -20, 0, 20, 40, 60};
        s.visPanel = 2;
        const std::string text = SerializeSettings(s);

        Settings parsed;
        const bool ok = ParseSettings(text, parsed);
        Check(ok, "round-trip: parse succeeds");
        Check(parsed.specMode == 4, "round-trip: specMode");
        Check(parsed.volumePercent == 80, "round-trip: volumePercent");
        Check(parsed.wasPlaying == true, "round-trip: wasPlaying");
        Check(parsed.currentIndex == 2, "round-trip: currentIndex");
        Check(parsed.xSound == true, "round-trip: xSound");
        Check(parsed.eqPreset == 1, "round-trip: eqPreset");
        Check(parsed.eqBands == s.eqBands, "round-trip: eqBands");
        Check(parsed.visPanel == 2, "round-trip: visPanel");
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
        Check(parsed.xSound == false, "missing key keeps default (xSound)");
        Check(parsed.eqPreset == -1, "missing key keeps default (eqPreset)");
        Check(parsed.eqBands == Settings{}.eqBands, "missing key keeps default (eqBands)");
        Check(parsed.visPanel == 0, "missing key keeps default (visPanel)");
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

    if (g_failures == 0) {
        std::printf("PASS\n");
        return 0;
    }
    std::printf("%d FAILURE(S)\n", g_failures);
    return 1;
}
