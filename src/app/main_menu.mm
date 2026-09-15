#include "app/main_menu.h"
#include "menu_internal.h"

#import <Cocoa/Cocoa.h>
#include <SDL2/SDL.h>
#include <vector>

// Kept private to this translation unit - main_menu.h stays
// Objective-C-free so it's safe for main.cpp (plain C++) to include.
// Shared by both the View and Effects menus below - it just pushes
// whatever event type it's configured with, tagged with the clicked
// item's tag, so a single generic target/action suffices for both. Also
// reused, via menu_internal.h's builders, by menu_bar_icon.mm's tray
// popup - a target object can back items in more than one NSMenu at
// once (only NSMenuItem instances themselves can't be shared).
@interface XmadMenuActionTarget : NSObject
@property(nonatomic, assign) uint32_t eventType;
- (void)onMenuAction:(id)sender;
@end

@implementation XmadMenuActionTarget
- (void)onMenuAction:(id)sender {
    SDL_Event ev;
    SDL_zero(ev);
    ev.type = self.eventType;
    ev.user.code = static_cast<Sint32>([sender tag]);
    SDL_PushEvent(&ev);
}
@end

namespace {
XmadMenuActionTarget* gTarget = nil;
// One entry per built instance of each checkable item (the real menu
// bar's, and - once ShowMenuBarIcon runs - the tray popup's), so
// SetUiScaleMenuChecked/SetEffectsMenuChecked below can keep all of
// them in sync with a single call.
std::vector<NSMenuItem*> g100Items;
std::vector<NSMenuItem*> g125Items;
std::vector<NSMenuItem*> g150Items;

XmadMenuActionTarget* gEffectsTarget = nil;
std::vector<NSMenuItem*> gXSoundItems;
} // namespace

namespace xmad::app {

namespace detail {

NSMenu* BuildViewMenu(uint32_t scaleEventType) {
    @autoreleasepool {
        if (!gTarget) {
            gTarget = [[XmadMenuActionTarget alloc] init];
            gTarget.eventType = scaleEventType;
        }

        auto addItem = [](NSMenu* menu, NSString* title, NSString* key, int tag) {
            NSMenuItem* item = [menu addItemWithTitle:title
                                                action:@selector(onMenuAction:)
                                         keyEquivalent:key];
            item.target = gTarget;
            item.tag = tag;
            return item;
        };

        NSMenu* viewMenu = [[NSMenu alloc] initWithTitle:@"View"];
        NSMenuItem* i100 = addItem(viewMenu, @"100%", @"", static_cast<int>(UiScaleMenuAction::Set100));
        NSMenuItem* i125 = addItem(viewMenu, @"125%", @"", static_cast<int>(UiScaleMenuAction::Set125));
        NSMenuItem* i150 = addItem(viewMenu, @"150%", @"", static_cast<int>(UiScaleMenuAction::Set150));
        [viewMenu addItem:[NSMenuItem separatorItem]];
        addItem(viewMenu, @"Zoom In", @"=", static_cast<int>(UiScaleMenuAction::ZoomIn));
        addItem(viewMenu, @"Zoom Out", @"-", static_cast<int>(UiScaleMenuAction::ZoomOut));
        addItem(viewMenu, @"Reset Zoom", @"0", static_cast<int>(UiScaleMenuAction::Reset));

        g100Items.push_back(i100);
        g125Items.push_back(i125);
        g150Items.push_back(i150);
        return viewMenu;
    }
}

NSMenu* BuildEffectsMenu(uint32_t effectsEventType) {
    @autoreleasepool {
        if (!gEffectsTarget) {
            gEffectsTarget = [[XmadMenuActionTarget alloc] init];
            gEffectsTarget.eventType = effectsEventType;
        }

        NSMenu* effectsMenu = [[NSMenu alloc] initWithTitle:@"Effects"];
        NSMenuItem* xSoundItem = [effectsMenu addItemWithTitle:@"xSound"
                                                          action:@selector(onMenuAction:)
                                                   keyEquivalent:@""];
        xSoundItem.target = gEffectsTarget;
        xSoundItem.tag = static_cast<int>(EffectsMenuAction::ToggleXSound);

        gXSoundItems.push_back(xSoundItem);
        return effectsMenu;
    }
}

} // namespace detail

void InstallUiScaleMenu(uint32_t scaleEventType) {
    @autoreleasepool {
        static bool installed = false; // idempotent, same spirit as ShowMenuBarIcon
        if (installed) return;
        installed = true;

        NSMenu* viewMenu = detail::BuildViewMenu(scaleEventType);
        NSMenuItem* viewMenuItem = [[NSMenuItem alloc] initWithTitle:@"View" action:nil keyEquivalent:@""];
        viewMenuItem.submenu = viewMenu;
        [[NSApp mainMenu] addItem:viewMenuItem];
    }
}

void SetUiScaleMenuChecked(int currentPercent) {
    @autoreleasepool {
        for (NSMenuItem* item : g100Items) item.state = currentPercent == 100 ? NSControlStateValueOn : NSControlStateValueOff;
        for (NSMenuItem* item : g125Items) item.state = currentPercent == 125 ? NSControlStateValueOn : NSControlStateValueOff;
        for (NSMenuItem* item : g150Items) item.state = currentPercent == 150 ? NSControlStateValueOn : NSControlStateValueOff;
    }
}

void InstallEffectsMenu(uint32_t effectsEventType) {
    @autoreleasepool {
        static bool installed = false; // idempotent, same spirit as InstallUiScaleMenu
        if (installed) return;
        installed = true;

        NSMenu* effectsMenu = detail::BuildEffectsMenu(effectsEventType);
        NSMenuItem* effectsMenuItem = [[NSMenuItem alloc] initWithTitle:@"Effects" action:nil keyEquivalent:@""];
        effectsMenuItem.submenu = effectsMenu;
        [[NSApp mainMenu] addItem:effectsMenuItem];
    }
}

void SetEffectsMenuChecked(bool xSoundOn) {
    @autoreleasepool {
        for (NSMenuItem* item : gXSoundItems) item.state = xSoundOn ? NSControlStateValueOn : NSControlStateValueOff;
    }
}

} // namespace xmad::app
