#include "app/menu_bar_icon.h"
#include "menu_internal.h"

#import <Cocoa/Cocoa.h>
#include <SDL2/SDL.h>

// Kept private to this translation unit - menu_bar_icon.h stays
// Objective-C-free so it's safe for main.cpp (plain C++) to include.
@interface XmadMenuBarTarget : NSObject
@property(nonatomic, assign) uint32_t restoreEventType;
@property(nonatomic, assign) uint32_t aboutEventType;
@property(nonatomic, strong) NSMenu* popupMenu;
- (void)onClick:(id)sender;
- (void)onAboutClick:(id)sender;
@end

@implementation XmadMenuBarTarget
- (void)onClick:(id)sender {
    // Right-click: show the View/Effects/About popup instead of
    // restoring - left-click (and anything else) keeps the original
    // restore behavior below. sendActionOn: (see ShowMenuBarIcon) is
    // what makes this handler fire for a right-click at all.
    if ([NSApp currentEvent].type == NSEventTypeRightMouseUp) {
        [NSMenu popUpContextMenu:self.popupMenu withEvent:[NSApp currentEvent] forView:(NSView*)sender];
        return;
    }
    // A dedicated custom event type, not a real SDL window event - see
    // menu_bar_icon.h's comment on ShowMenuBarIcon for why.
    SDL_Event ev;
    SDL_zero(ev);
    ev.type = self.restoreEventType;
    SDL_PushEvent(&ev);
}
- (void)onAboutClick:(id)sender {
    SDL_Event ev;
    SDL_zero(ev);
    ev.type = self.aboutEventType;
    SDL_PushEvent(&ev);
}
- (void)onQuitClick:(id)sender {
    // A real SDL_QUIT, not a custom bridged event type - processEvent's
    // very first check (`if (ev.type == SDL_QUIT) running = false;`)
    // already handles this exactly like Cmd+Q/the real Quit menu item
    // do, so there's nothing app-specific to route here.
    SDL_Event ev;
    SDL_zero(ev);
    ev.type = SDL_QUIT;
    SDL_PushEvent(&ev);
}
@end

namespace {
NSStatusItem* gStatusItem = nil;
XmadMenuBarTarget* gTarget = nil;
} // namespace

namespace xmad::app {

void ShowMenuBarIcon(uint32_t restoreEventType, uint32_t scaleEventType, uint32_t effectsEventType,
                      uint32_t aboutEventType) {
    @autoreleasepool {
        if (gStatusItem) return;
        gTarget = [[XmadMenuBarTarget alloc] init];
        gTarget.restoreEventType = restoreEventType;
        gTarget.aboutEventType = aboutEventType;

        NSMenu* popupMenu = [[NSMenu alloc] init];
        NSMenuItem* viewItem = [[NSMenuItem alloc] initWithTitle:@"View" action:nil keyEquivalent:@""];
        viewItem.submenu = detail::BuildViewMenu(scaleEventType);
        [popupMenu addItem:viewItem];
        NSMenuItem* effectsItem = [[NSMenuItem alloc] initWithTitle:@"Effects" action:nil keyEquivalent:@""];
        effectsItem.submenu = detail::BuildEffectsMenu(effectsEventType);
        [popupMenu addItem:effectsItem];
        NSMenuItem* quitItem = [popupMenu addItemWithTitle:@"Quit" action:@selector(onQuitClick:) keyEquivalent:@""];
        quitItem.target = gTarget;
        [popupMenu addItem:[NSMenuItem separatorItem]];
        NSMenuItem* aboutItem = [popupMenu addItemWithTitle:@"About"
                                                       action:@selector(onAboutClick:)
                                                keyEquivalent:@""];
        aboutItem.target = gTarget;
        gTarget.popupMenu = popupMenu;

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
        // Default status-item button behavior only sends the action for
        // a left click; add right-click so onClick: above can branch on
        // it to show the popup menu instead of restoring.
        [gStatusItem.button sendActionOn:(NSEventMaskLeftMouseUp | NSEventMaskRightMouseUp)];
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

void ActivateApp() {
    @autoreleasepool {
        // Same reasoning as SetDockIconVisible's activate call below, but
        // without touching the Dock icon/activation policy - for showing
        // a single window (e.g. About from the tray popup) on top of an
        // otherwise still-minimized app, rather than a full restore.
        [NSApp activateIgnoringOtherApps:YES];
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
