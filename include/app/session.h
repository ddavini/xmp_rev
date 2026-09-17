#pragma once

#include <array>
#include <string>
#include <unordered_map>

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
// ShowTask, etc - those weren't asked for.

namespace xmad::app {

struct Settings {
    int specMode = 0;       // VisMode enum value (see main.cpp), 0..5
    int volumePercent = 25; // 0..100, matches Engine's own fresh-install default
    bool wasPlaying = false;
    // Fixes "pause then quit loses the position": wasPlaying alone can't
    // tell a deliberate Stop (original: don't resume) apart from a Pause
    // (user meant to come back to this). Track/position now reopen on
    // either wasPlaying or wasPaused; wasPlaying alone still decides
    // whether playback actually resumes (see main.cpp's resume block).
    bool wasPaused = false;
    int currentIndex = 0;   // index into the resumed playlist
    double positionSeconds = 0.0; // exact mid-track resume position - only
                                   // applied when wasPlaying or wasPaused
                                   // (see main.cpp)
    bool xSound = false;    // Engine::XSound() - mirrors mnuXSound.Checked
    bool reverb = false;      // Engine::ReverbOn()
    bool saturation = false;  // Engine::SaturationOn()
    bool compression = false; // Engine::CompressionOn()
    bool chorus = false;      // Engine::ChorusOn()
    // TODO: "Repeat / Shuffle (Random) playback modes". Mirrors the
    // original's g_Ripeti/Acaso (Loop/Random) - see
    // app::NextAutoAdvanceIndex for how they're actually consulted at
    // end-of-track. Not gated on any Engine state (there's no audio
    // effect involved), just plain UI-level playback flags.
    bool repeat = false;
    bool random = false;
    // TODO: "EQ mode not saved". eqPreset mirrors main.cpp's
    // eqCurrentPreset (-1 = none/manual, matching Form_Load never
    // checking a radio button in the original); eqBands are the actual
    // per-band gains (-127..127, audio::Equalizer::kBands of them) -
    // saved separately from the preset index since a manual tweak after
    // picking a preset should still be restored exactly, not snapped
    // back to that preset's fixed values.
    int eqPreset = -1;
    std::array<int, 10> eqBands{};
    // TODO: "per song equalization setting". Off (default) = today's
    // behavior, eqBands above applies globally to every track. On = each
    // track's own bands are looked up/recalled on open instead - see
    // EqPerSongPath()/SerializeEqPerSong below, kept in a separate file
    // rather than here since it's keyed data (per track), not a single
    // flat value like the rest of this struct.
    bool perSongEq = false;
    // TODO: "visualizations missing from the original" - which of the
    // three panels sharing the analyzer's box is showing: 0=analyzer
    // (see main.cpp's VisPanel enum for the full mapping), 1=idle logo,
    // 2=CardioOSC. Mirrors the original's own ANALYZER/CPULESS/COSC INI
    // flags (FunzioniGlobali.bas), collapsed into one field since exactly
    // one of those three is ever true at a time in practice.
    int visPanel = 0;
    // TODO: "Add a setting for UI scale / text+button size" - one of
    // {100, 125, 150} (main.cpp's SnapUiScalePercent snaps anything else,
    // e.g. a corrupt/future value, back to the 100 default). Applies to
    // every window's size/position and the mouse-hit-test coordinate
    // conversion, not just drawing - see main.cpp's `scale`.
    int uiScalePercent = 100;
    // TODO: "low resource mode, 30FPS ... Activation in a new menu of
    // Options called Potato" - two independent flags, loaded unconditionally
    // at startup (see main.cpp's uiScalePercent early-load block) rather
    // than gated on resumeSession like repeat/random: these are performance
    // prefs, not playback state.
    bool potatoLowFps = false;          // Potato > "30 FPS": halves the render loop's frame cap
    bool potatoCheapVisualizer = false; // Potato > "Cheap Visualizer": forces VU/peak-bar-only rendering
    // Potato > "Force Software Rendering": opts back out of the GPU-
    // accelerated SDL renderer (see main.cpp's CreatePreferredRenderer) in
    // favor of the old CPU-side software one - an escape hatch for a
    // GPU/driver combo that misbehaves under acceleration, not something
    // meant to be toggled routinely. Read only at startup (renderers are
    // created once, long before any settings-menu interaction is possible),
    // so unlike the two flags above this one takes effect on next launch,
    // not live - the menu label says so.
    bool forceSoftwareRenderer = false;
};

// Pure parsing/serialization - no filesystem access, so directly
// unit-testable (see session_test.cpp), same split as
// app::ParseDialogOutput in file_dialog.h. ParseSettings only fails (returns
// false) on empty input; unrecognized or malformed lines are skipped rather
// than rejecting the whole file, so a settings file from a future version
// with extra keys still loads today's known fields.
std::string SerializeSettings(const Settings& s);
bool ParseSettings(const std::string& text, Settings& out);

// Per-track EQ bands for the perSongEq feature above - one line per track,
// "path=band0,band1,...,band9", same comma-separated-bands convention as
// SerializeSettings' EQBANDS line, just keyed by path instead of a fixed
// key name. Same tolerant-parsing philosophy as ParseSettings: a
// malformed or short line is skipped/truncated rather than rejecting the
// whole file. A track with no entry here (e.g. never played with
// perSongEq on) has no saved bands at all - the caller's job to decide
// what "flat" means, not this parser's.
std::string SerializeEqPerSong(const std::unordered_map<std::string, std::array<int, 10>>& bands);
bool ParseEqPerSong(const std::string& text, std::unordered_map<std::string, std::array<int, 10>>& out);

// Where the settings file, the resumed session's playlist, and the
// per-song EQ file live. A flat ~/.xmad-revival directory (created on
// first save if missing), not the original's App.Path\xmp.ini - this
// targets an installed app on macOS/Linux, neither of which has Windows'
// "next to the executable" convention.
std::string SettingsFilePath();
std::string SessionPlaylistPath();
std::string EqPerSongPath();

// Thin OS-touching wrappers around the pure functions above.
bool SaveSettingsFile(const std::string& path, const Settings& s);
bool LoadSettingsFile(const std::string& path, Settings& out);
bool SaveEqPerSongFile(const std::string& path, const std::unordered_map<std::string, std::array<int, 10>>& bands);
bool LoadEqPerSongFile(const std::string& path, std::unordered_map<std::string, std::array<int, 10>>& out);

} // namespace xmad::app
