#pragma once

#include <cstdint>

// macOS-only: hides/restores the Dock icon and shows a clickable
// NSStatusItem (menu-bar icon) as a stand-in access point while the app
// is minimized. Implemented in src/app/menu_bar_icon.mm (Objective-C++,
// ARC) - deliberately NOT routed through osascript like
// file_dialog.cpp's OpenNativeFileDialog: a persistent, clickable status
// item needs a live target-action callback wired back into the running
// process, which a one-shot osascript call can't give us. This is the
// only place in the codebase that links AppKit/Cocoa directly.
//
// Only ever included under #ifdef __APPLE__ in main.cpp, and only ever
// compiled on Darwin (see Makefile) - no stub/no-op implementation
// exists or is needed for other platforms.

namespace xmad::app {

// Creates (if not already showing) a status-bar item displaying the
// app's icon, falling back to a plain text title if AppIcon.icns can't
// be resolved from the running bundle's Resources (e.g. when launched
// as the raw unbundled binary rather than via the .app bundle - the
// icon lookup degrades gracefully rather than crashing or leaving a
// blank/invisible item). Left-clicking it pushes an SDL event of type
// `restoreEventType` (an SDL_RegisterEvents-allocated code owned by the
// caller) into the SDL event queue - deliberately a dedicated custom
// event, not a real SDL_WINDOWEVENT_RESTORED/FOCUS_GAINED: an earlier
// version reused those, but hiding a window + switching the app's
// activation policy turned out to itself generate a spurious event of
// that same shape for Main a moment later, which was indistinguishable
// from a real click and caused the app to immediately un-hide itself.
// Right-clicking it instead pops up a context menu with "View",
// "Effects", "Playback" and "Potato" submenus - the same controls as the
// real menu bar (see main_menu.h) - built from scaleEventType/
// effectsEventType/playbackEventType/potatoEventType, the same event types
// the caller already passed to InstallUiScaleMenu/InstallOptionsMenu, so
// picking an item there routes through the exact same processEvent
// handling either menu would. Kept as flat sibling items here rather than
// nesting Effects/Playback/Potato under an "Options" item the way the real
// menu bar does - a right-click tray popup is conventionally flatter than
// a menu bar. Below those, a "Quit" item pushes a real SDL_QUIT (same
// effect as Cmd+Q/the real Quit menu item - no dedicated event type
// needed), then a separator and an "About" item push aboutEventType (an
// SDL_RegisterEvents-allocated code owned by the caller, same pattern as
// the other event types here) when clicked. Idempotent: a second call
// while already showing is a no-op.
void ShowMenuBarIcon(uint32_t restoreEventType, uint32_t scaleEventType, uint32_t effectsEventType,
                      uint32_t playbackEventType, uint32_t potatoEventType, uint32_t aboutEventType);

// Removes the status item, if present. Idempotent/safe when none exists.
void HideMenuBarIcon();

// Calls [NSApp activateIgnoringOtherApps:YES] without touching the Dock
// icon/activation policy - unlike SetDockIconVisible(true) below, which
// changes the policy too. For bringing a single window to the front
// (e.g. About, from the tray popup's "About" item) without doing a full
// restore-from-tray.
void ActivateApp();

// Switches between NSApplicationActivationPolicyRegular (normal Dock
// icon + Cmd-Tab entry) and NSApplicationActivationPolicyAccessory (no
// Dock icon, no Cmd-Tab entry). Call-order at the two transition points
// matters - see the two guarded call sites in main.cpp's processEvent,
// which always add the new access point before removing the old one.
void SetDockIconVisible(bool visible);

// levels: `count` values. Bar mode (oscilloscope=false): each in [0,1],
// bar height. Oscilloscope mode (oscilloscope=true): each in [-1,1],
// waveform samples left-to-right in time. Called from main.cpp's tray
// tick while appHiddenToTray is true and playback is active.
void UpdateMenuBarVisualizer(const float* levels, int count, bool oscilloscope);

// Reverts the status item's icon back to the cached static app icon.
// No-op if not currently showing the visualizer.
void ClearMenuBarVisualizer();

} // namespace xmad::app
