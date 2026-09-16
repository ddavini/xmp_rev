#pragma once

#include <cstdint>

// macOS-only: appends "View" and "Effects" menus onto NSApp's existing
// mainMenu - deliberately does NOT replace/rebuild mainMenu itself, since
// SDL2's Cocoa backend already installs a default one (via
// Cocoa_RegisterApp, triggered by SDL_Init) with a working Quit item
// whose Cmd+Q routes through a real SDL_QUIT event - reusing that rather
// than reimplementing it. Must be called after SDL_Init (that's what
// creates NSApp/its default menu in the first place). Same
// Objective-C-free-header / SDL-event-bridging pattern as
// menu_bar_icon.h - kept as a separate file since that one's purpose is
// the tray/status-item feature specifically, not the app's menu bar.
//
// View: UI scale (100/200/300/400% radio items plus Zoom In/Out/Reset,
// mirroring familiar app zoom conventions).
//
// Options: a top-level menu (matching the original's real "Option" menu
// name - Source/Menu.frm's mnuXmPButton "Option") holding two submenus:
//
//   Effects: xSound (mirroring the "x" key/mnuXSound toggle - see
//   main.cpp's toggleXSound), plus Reverb/Saturation/Compression/Chorus -
//   all checkable items rather than radio items, since each is an
//   independent on/off flag with fixed internal parameters (no in-menu
//   tuning).
//
//   Playback: Repeat/Random, mirroring the original's own mnuLoop/mnuRnd
//   (which lived directly under that same "Option" menu, not a further
//   submenu there - grouping them under "Playback" here is this port's
//   own organizational choice, not a literal port of the original's flat
//   menu layout).

namespace xmad::app {

// scaleEventType: an SDL_RegisterEvents-allocated code (same bridging
// pattern as menu_bar_icon.h/media_remote.h) pushed with event.user.code
// set to one of these values whenever a menu item or its Cmd+/-/0
// keyboard equivalent fires.
enum class UiScaleMenuAction : int32_t {
    Set100 = 0,
    Set200 = 1,
    Set300 = 2,
    Set400 = 3,
    ZoomIn = 4,
    ZoomOut = 5,
    Reset = 6,
};

void InstallUiScaleMenu(uint32_t scaleEventType);

// Updates the four radio items' checkmarks to reflect the current scale
// (100/200/300/400) - call once after InstallUiScaleMenu with the resumed
// setting, and again every time the scale actually changes.
void SetUiScaleMenuChecked(int currentPercent);

// effectsEventType: same bridging pattern as scaleEventType above,
// pushed with event.user.code set to one of these values whenever a
// menu item fires.
enum class EffectsMenuAction : int32_t {
    ToggleXSound = 0,
    ToggleReverb = 1,
    ToggleSaturation = 2,
    ToggleCompression = 3,
    ToggleChorus = 4,
};

// Installs the top-level "Options" menu (Effects + Playback submenus) in
// one call - replaces the old InstallEffectsMenu, which added "Effects"
// directly to the menu bar; effectsEventType/playbackEventType are each
// an SDL_RegisterEvents-allocated code, same bridging pattern as
// scaleEventType above, one per submenu since they dispatch different
// action enums.
void InstallOptionsMenu(uint32_t effectsEventType, uint32_t playbackEventType);

// Bundles all 5 toggle states in one call - avoids an error-prone
// 5-bool-parameter-order call site now that there's more than just xSound.
struct EffectsMenuState {
    bool xSound = false;
    bool reverb = false;
    bool saturation = false;
    bool compression = false;
    bool chorus = false;
};

// Updates every effect item's checkmark to reflect its current on/off
// state - call once after InstallOptionsMenu with the resumed/initial
// state, and again every time any of them is toggled (from a menu, the
// "x" key, or a resumed session).
void SetEffectsMenuChecked(const EffectsMenuState& state);

// playbackEventType: same bridging pattern as effectsEventType above.
enum class PlaybackMenuAction : int32_t {
    ToggleRepeat = 0,
    ToggleRandom = 1,
};

// Bundles both Playback toggle states in one call, same reasoning as
// EffectsMenuState above.
struct PlaybackMenuState {
    bool repeat = false;
    bool random = false;
};

// Updates the Repeat/Random items' checkmarks - call once after
// InstallOptionsMenu with the resumed/initial state, and again every time
// either is toggled (from a menu or a resumed session).
void SetPlaybackMenuChecked(const PlaybackMenuState& state);

} // namespace xmad::app
