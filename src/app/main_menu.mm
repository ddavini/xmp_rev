#include "app/main_menu.h"

#import <Cocoa/Cocoa.h>
#include <SDL2/SDL.h>

// Kept private to this translation unit - main_menu.h stays
// Objective-C-free so it's safe for main.cpp (plain C++) to include.
@interface XmadScaleMenuTarget : NSObject
@property(nonatomic, assign) uint32_t scaleEventType;
- (void)onScaleAction:(id)sender;
@end

@implementation XmadScaleMenuTarget
- (void)onScaleAction:(id)sender {
    SDL_Event ev;
    SDL_zero(ev);
    ev.type = self.scaleEventType;
    ev.user.code = static_cast<Sint32>([sender tag]);
    SDL_PushEvent(&ev);
}
@end

namespace {
XmadScaleMenuTarget* gTarget = nil;
NSMenuItem* g100Item = nil;
NSMenuItem* g125Item = nil;
NSMenuItem* g150Item = nil;
} // namespace

namespace xmad::app {

void InstallUiScaleMenu(uint32_t scaleEventType) {
    @autoreleasepool {
        if (gTarget) return; // idempotent, same spirit as ShowMenuBarIcon
        gTarget = [[XmadScaleMenuTarget alloc] init];
        gTarget.scaleEventType = scaleEventType;

        auto addItem = [](NSMenu* menu, NSString* title, NSString* key, int tag) {
            NSMenuItem* item = [menu addItemWithTitle:title
                                                action:@selector(onScaleAction:)
                                         keyEquivalent:key];
            item.target = gTarget;
            item.tag = tag;
            return item;
        };

        NSMenu* viewMenu = [[NSMenu alloc] initWithTitle:@"View"];
        g100Item = addItem(viewMenu, @"100%", @"", static_cast<int>(UiScaleMenuAction::Set100));
        g125Item = addItem(viewMenu, @"125%", @"", static_cast<int>(UiScaleMenuAction::Set125));
        g150Item = addItem(viewMenu, @"150%", @"", static_cast<int>(UiScaleMenuAction::Set150));
        [viewMenu addItem:[NSMenuItem separatorItem]];
        addItem(viewMenu, @"Zoom In", @"=", static_cast<int>(UiScaleMenuAction::ZoomIn));
        addItem(viewMenu, @"Zoom Out", @"-", static_cast<int>(UiScaleMenuAction::ZoomOut));
        addItem(viewMenu, @"Reset Zoom", @"0", static_cast<int>(UiScaleMenuAction::Reset));

        NSMenuItem* viewMenuItem = [[NSMenuItem alloc] initWithTitle:@"View" action:nil keyEquivalent:@""];
        viewMenuItem.submenu = viewMenu;
        [[NSApp mainMenu] addItem:viewMenuItem];
    }
}

void SetUiScaleMenuChecked(int currentPercent) {
    @autoreleasepool {
        if (!g100Item) return; // InstallUiScaleMenu not called yet - nothing to update
        g100Item.state = currentPercent == 100 ? NSControlStateValueOn : NSControlStateValueOff;
        g125Item.state = currentPercent == 125 ? NSControlStateValueOn : NSControlStateValueOff;
        g150Item.state = currentPercent == 150 ? NSControlStateValueOn : NSControlStateValueOff;
    }
}

} // namespace xmad::app
