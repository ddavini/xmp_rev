#pragma once

#include <cstdint>

// macOS-only: appends a "View" menu (UI scale: 100/125/150% radio items
// plus Zoom In/Out/Reset, mirroring familiar app zoom conventions) onto
// NSApp's existing mainMenu - deliberately does NOT replace/rebuild
// mainMenu itself, since SDL2's Cocoa backend already installs a default
// one (via Cocoa_RegisterApp, triggered by SDL_Init) with a working Quit
// item whose Cmd+Q routes through a real SDL_QUIT event - reusing that
// rather than reimplementing it. Must be called after SDL_Init (that's
// what creates NSApp/its default menu in the first place). Same
// Objective-C-free-header / SDL-event-bridging pattern as
// menu_bar_icon.h - kept as a separate file since that one's purpose is
// the tray/status-item feature specifically, not the app's menu bar.

namespace xmad::app {

// scaleEventType: an SDL_RegisterEvents-allocated code (same bridging
// pattern as menu_bar_icon.h/media_remote.h) pushed with event.user.code
// set to one of these values whenever a menu item or its Cmd+/-/0
// keyboard equivalent fires.
enum class UiScaleMenuAction : int32_t {
    Set100 = 0,
    Set125 = 1,
    Set150 = 2,
    ZoomIn = 3,
    ZoomOut = 4,
    Reset = 5,
};

void InstallUiScaleMenu(uint32_t scaleEventType);

// Updates the three radio items' checkmarks to reflect the current scale
// (100/125/150) - call once after InstallUiScaleMenu with the resumed
// setting, and again every time the scale actually changes.
void SetUiScaleMenuChecked(int currentPercent);

} // namespace xmad::app
