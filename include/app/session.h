#pragma once

#include <array>
#include <string>

// Session persistence: TODO's "save settings on exit (visualizer active,
// volume, playing, stopped, playlist)", plus XSound (added after the user
// noticed it wasn't included - same on/off nature as the other toggles),
// plus the exact mid-track position (TODO's "saves the position in the
// song so you can restart where you left off" - a later, explicit reversal
// of this comment's own prior note that it was skipped to match the
// original's default-off "SSTREAMPOS" preference). *Whether* a track
// auto-resumes playing is still gated on wasPlaying, same as always - only
// *where* it resumes from changed, via positionSeconds below.
// Deliberately narrower than the original's xmp.ini
// (Source/FunzioniGlobali.bas's GestisciPosFrm): no window position,
// Repeat/Random (not implemented yet), ShowTask, etc - those weren't
// asked for.

namespace xmad::app {

struct Settings {
    int specMode = 0;       // VisMode enum value (see main.cpp), 0..5
    int volumePercent = 25; // 0..100, matches Engine's own fresh-install default
    bool wasPlaying = false;
    int currentIndex = 0;   // index into the resumed playlist
    double positionSeconds = 0.0; // exact mid-track resume position - only
                                   // applied when wasPlaying (see main.cpp)
    bool xSound = false;    // Engine::XSound() - mirrors mnuXSound.Checked
    // TODO: "EQ mode not saved". eqPreset mirrors main.cpp's
    // eqCurrentPreset (-1 = none/manual, matching Form_Load never
    // checking a radio button in the original); eqBands are the actual
    // per-band gains (-127..127, audio::Equalizer::kBands of them) -
    // saved separately from the preset index since a manual tweak after
    // picking a preset should still be restored exactly, not snapped
    // back to that preset's fixed values.
    int eqPreset = -1;
    std::array<int, 10> eqBands{};
    // TODO: "visualizations missing from the original" - which of the
    // three panels sharing the analyzer's box is showing: 0=analyzer
    // (see main.cpp's VisPanel enum for the full mapping), 1=idle logo,
    // 2=CardioOSC. Mirrors the original's own ANALYZER/CPULESS/COSC INI
    // flags (FunzioniGlobali.bas), collapsed into one field since exactly
    // one of those three is ever true at a time in practice.
    int visPanel = 0;
};

// Pure parsing/serialization - no filesystem access, so directly
// unit-testable (see session_test.cpp), same split as
// app::ParseDialogOutput in file_dialog.h. ParseSettings only fails (returns
// false) on empty input; unrecognized or malformed lines are skipped rather
// than rejecting the whole file, so a settings file from a future version
// with extra keys still loads today's known fields.
std::string SerializeSettings(const Settings& s);
bool ParseSettings(const std::string& text, Settings& out);

// Where the settings file and the resumed session's playlist live. A flat
// ~/.xmad-revival directory (created on first save if missing), not the
// original's App.Path\xmp.ini - this targets an installed app on
// macOS/Linux, neither of which has Windows' "next to the executable"
// convention.
std::string SettingsFilePath();
std::string SessionPlaylistPath();

// Thin OS-touching wrappers around the pure functions above.
bool SaveSettingsFile(const std::string& path, const Settings& s);
bool LoadSettingsFile(const std::string& path, Settings& out);

} // namespace xmad::app
