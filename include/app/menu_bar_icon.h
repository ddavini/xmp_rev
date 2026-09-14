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
// blank/invisible item). Clicking it pushes an SDL event of type
// `restoreEventType` (an SDL_RegisterEvents-allocated code owned by the
// caller) into the SDL event queue - deliberately a dedicated custom
// event, not a real SDL_WINDOWEVENT_RESTORED/FOCUS_GAINED: an earlier
// version reused those, but hiding a window + switching the app's
// activation policy turned out to itself generate a spurious event of
// that same shape for Main a moment later, which was indistinguishable
// from a real click and caused the app to immediately un-hide itself.
// Idempotent: a second call while already showing is a no-op.
void ShowMenuBarIcon(uint32_t restoreEventType);

// Removes the status item, if present. Idempotent/safe when none exists.
void HideMenuBarIcon();

// Switches between NSApplicationActivationPolicyRegular (normal Dock
// icon + Cmd-Tab entry) and NSApplicationActivationPolicyAccessory (no
// Dock icon, no Cmd-Tab entry). Call-order at the two transition points
// matters - see the two guarded call sites in main.cpp's processEvent,
// which always add the new access point before removing the old one.
void SetDockIconVisible(bool visible);

} // namespace xmad::app
