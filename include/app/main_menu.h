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
// Effects: xSound (mirroring the "x" key/mnuXSound toggle - see
// main.cpp's toggleXSound), plus Reverb/Saturation/Compression/Chorus -
// all checkable items rather than radio items, since each is an
// independent on/off flag with fixed internal parameters (no in-menu
// tuning).

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

void InstallEffectsMenu(uint32_t effectsEventType);

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
// state - call once after InstallEffectsMenu with the resumed/initial
// state, and again every time any of them is toggled (from a menu, the
// "x" key, or a resumed session).
void SetEffectsMenuChecked(const EffectsMenuState& state);

} // namespace xmad::app
