#pragma once

// Private to the macOS menu-building .mm files - main_menu.mm builds the
// View/Effects submenu contents, menu_bar_icon.mm reuses them to build
// the tray icon's right-click popup. NOT included by main.cpp, unlike
// main_menu.h/menu_bar_icon.h, which must stay Objective-C-free; kept as
// its own file rather than exposing NSMenu* through those public headers.

#import <Cocoa/Cocoa.h>
#include <cstdint>

namespace xmad::app::detail {

// Builds a fresh "View" NSMenu (UI scale 100/125/150% + Zoom In/Out/
// Reset), pushing events of scaleEventType exactly like the real menu
// bar's own View menu. Each call returns an independent NSMenu/
// NSMenuItem set - Cocoa doesn't allow the same NSMenuItem instance to
// belong to two parent menus at once - but the created checkable items
// are registered with main_menu.mm's internal tracking so
// SetUiScaleMenuChecked keeps every instance (menu bar, tray popup, ...)
// in sync.
NSMenu* BuildViewMenu(uint32_t scaleEventType);

// Same idea for the "Effects" menu (xSound, Reverb, Saturation,
// Compression, Chorus).
NSMenu* BuildEffectsMenu(uint32_t effectsEventType);

} // namespace xmad::app::detail
