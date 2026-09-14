#include "app/menu_bar_icon.h"

#import <Cocoa/Cocoa.h>
#include <SDL2/SDL.h>

// Kept private to this translation unit - menu_bar_icon.h stays
// Objective-C-free so it's safe for main.cpp (plain C++) to include.
@interface XmadMenuBarTarget : NSObject
@property(nonatomic, assign) uint32_t restoreEventType;
- (void)onClick:(id)sender;
@end

@implementation XmadMenuBarTarget
- (void)onClick:(id)sender {
    // A dedicated custom event type, not a real SDL window event - see
    // menu_bar_icon.h's comment on ShowMenuBarIcon for why.
    SDL_Event ev;
    SDL_zero(ev);
    ev.type = self.restoreEventType;
    SDL_PushEvent(&ev);
}
@end

namespace {
NSStatusItem* gStatusItem = nil;
XmadMenuBarTarget* gTarget = nil;
} // namespace

namespace xmad::app {

void ShowMenuBarIcon(uint32_t restoreEventType) {
    @autoreleasepool {
        if (gStatusItem) return;
        gTarget = [[XmadMenuBarTarget alloc] init];
        gTarget.restoreEventType = restoreEventType;

        gStatusItem = [[NSStatusBar systemStatusBar] statusItemWithLength:NSVariableStatusItemLength];
        // Title set unconditionally, not just as an icon-load fallback:
        // guarantees something visible even if the icon below fails to
        // render for some reason - cheap insurance, since a totally blank
        // status item is indistinguishable from "nothing happened".
        gStatusItem.button.title = @"X.MaD";
        NSString* iconPath = [[NSBundle mainBundle] pathForResource:@"AppIcon" ofType:@"icns"];
        NSImage* icon = iconPath ? [[NSImage alloc] initWithContentsOfFile:iconPath] : nil;
        if (icon) {
            icon.size = NSMakeSize(18, 18);
            // Bracket form, not dot syntax: NSImage's `template` property
            // name collides with the C++ keyword, so `icon.template = NO`
            // does not compile under Objective-C++.
            [icon setTemplate:NO];
            gStatusItem.button.image = icon;
        }
        gStatusItem.button.target = gTarget;
        gStatusItem.button.action = @selector(onClick:);
    }
}

void HideMenuBarIcon() {
    @autoreleasepool {
        if (!gStatusItem) return;
        [[NSStatusBar systemStatusBar] removeStatusItem:gStatusItem];
        gStatusItem = nil;
        gTarget = nil;
    }
}

void SetDockIconVisible(bool visible) {
    @autoreleasepool {
        [NSApp setActivationPolicy:visible ? NSApplicationActivationPolicyRegular
                                            : NSApplicationActivationPolicyAccessory];
        if (visible) {
            // An Accessory-policy app isn't guaranteed to come to the
            // foreground just because one of its windows gets ordered
            // front - without this, clicking the menu-bar icon correctly
            // ran the restore code (windows shown, Dock icon back) but
            // the app silently stayed in the background, which read as
            // "nothing happened" (only the menu-bar icon visibly went
            // away). Must happen before the caller shows/raises windows.
            [NSApp activateIgnoringOtherApps:YES];
        }
    }
}

} // namespace xmad::app
