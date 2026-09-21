#include "app/main_menu.h"
#include "menu_internal.h"

#import <Cocoa/Cocoa.h>
#include <SDL2/SDL.h>
#include <functional>
#include <unordered_map>
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
// Backing state for SetMenuTrackingRedrawCallback (see main_menu.h's
// comment on it). gMenuTrackingDepth (not a plain bool) tracks nested
// tracking sessions (e.g. Options -> Potato submenu) so the timer only
// stops once every level has closed.
std::function<void()> gMenuTrackingRedrawCallback;
NSTimer* gMenuTrackingTimer = nil;
int gMenuTrackingDepth = 0;
id gMenuTrackingObserver = nil; // XmadMenuTrackingObserver*, declared further below
} // namespace

// Listens app-wide for any NSMenu beginning/ending tracking (menu bar
// items and popup context menus alike) and runs a timer only for that
// window, in NSEventTrackingRunLoopMode specifically - a mode our own
// main() loop never runs in, and one Cocoa keeps servicing during
// tracking, unlike the default run loop mode our SDL loop relies on.
@interface XmadMenuTrackingObserver : NSObject
- (void)menuDidBeginTracking:(NSNotification*)note;
- (void)menuDidEndTracking:(NSNotification*)note;
@end

@implementation XmadMenuTrackingObserver
- (void)menuDidBeginTracking:(NSNotification*)note {
    (void)note;
    if (gMenuTrackingDepth++ == 0) {
        gMenuTrackingTimer = [NSTimer timerWithTimeInterval:1.0 / 30.0
                                                     repeats:YES
                                                       block:^(NSTimer* timer) {
            (void)timer;
            if (gMenuTrackingRedrawCallback) gMenuTrackingRedrawCallback();
        }];
        [[NSRunLoop mainRunLoop] addTimer:gMenuTrackingTimer forMode:NSEventTrackingRunLoopMode];
    }
}
- (void)menuDidEndTracking:(NSNotification*)note {
    (void)note;
    if (gMenuTrackingDepth > 0 && --gMenuTrackingDepth == 0) {
        [gMenuTrackingTimer invalidate];
        gMenuTrackingTimer = nil;
    }
}
@end

namespace {
XmadMenuActionTarget* gTarget = nil;
// One entry per built instance of each checkable item (the real menu
// bar's, and - once ShowMenuBarIcon runs - the tray popup's), so
// SetUiScaleMenuChecked/SetEffectsMenuChecked below can keep all of
// them in sync with a single call.
std::vector<NSMenuItem*> g100Items;
std::vector<NSMenuItem*> g200Items;
std::vector<NSMenuItem*> g300Items;
std::vector<NSMenuItem*> g400Items;

XmadMenuActionTarget* gEffectsTarget = nil;
// Keyed by EffectsMenuAction tag; one entry per built instance of each
// checkable item (the real menu bar's, and - once ShowMenuBarIcon runs -
// the tray popup's), so SetEffectsMenuChecked below can keep every
// instance of every effect in sync with a single call.
std::unordered_map<int32_t, std::vector<NSMenuItem*>> gEffectsItemsByTag;

XmadMenuActionTarget* gPlaybackTarget = nil;
// Same idea as gEffectsItemsByTag, keyed by PlaybackMenuAction tag.
std::unordered_map<int32_t, std::vector<NSMenuItem*>> gPlaybackItemsByTag;

XmadMenuActionTarget* gPotatoTarget = nil;
// Same idea as gEffectsItemsByTag, keyed by PotatoMenuAction tag.
std::unordered_map<int32_t, std::vector<NSMenuItem*>> gPotatoItemsByTag;
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
        NSMenuItem* i200 = addItem(viewMenu, @"200%", @"", static_cast<int>(UiScaleMenuAction::Set200));
        NSMenuItem* i300 = addItem(viewMenu, @"300%", @"", static_cast<int>(UiScaleMenuAction::Set300));
        NSMenuItem* i400 = addItem(viewMenu, @"400%", @"", static_cast<int>(UiScaleMenuAction::Set400));
        [viewMenu addItem:[NSMenuItem separatorItem]];
        addItem(viewMenu, @"Zoom In", @"=", static_cast<int>(UiScaleMenuAction::ZoomIn));
        addItem(viewMenu, @"Zoom Out", @"-", static_cast<int>(UiScaleMenuAction::ZoomOut));
        addItem(viewMenu, @"Reset Zoom", @"0", static_cast<int>(UiScaleMenuAction::Reset));

        g100Items.push_back(i100);
        g200Items.push_back(i200);
        g300Items.push_back(i300);
        g400Items.push_back(i400);
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
        auto addToggle = [&](NSString* title, EffectsMenuAction action) {
            NSMenuItem* item = [effectsMenu addItemWithTitle:title
                                                        action:@selector(onMenuAction:)
                                                 keyEquivalent:@""];
            item.target = gEffectsTarget;
            item.tag = static_cast<int>(action);
            gEffectsItemsByTag[static_cast<int32_t>(action)].push_back(item);
        };
        addToggle(@"xSound", EffectsMenuAction::ToggleXSound);
        addToggle(@"Reverb", EffectsMenuAction::ToggleReverb);
        addToggle(@"Saturation", EffectsMenuAction::ToggleSaturation);
        addToggle(@"Compression", EffectsMenuAction::ToggleCompression);
        addToggle(@"Chorus", EffectsMenuAction::ToggleChorus);
        return effectsMenu;
    }
}

NSMenu* BuildPlaybackMenu(uint32_t playbackEventType) {
    @autoreleasepool {
        if (!gPlaybackTarget) {
            gPlaybackTarget = [[XmadMenuActionTarget alloc] init];
            gPlaybackTarget.eventType = playbackEventType;
        }

        NSMenu* playbackMenu = [[NSMenu alloc] initWithTitle:@"Playback"];
        auto addToggle = [&](NSString* title, PlaybackMenuAction action) {
            NSMenuItem* item = [playbackMenu addItemWithTitle:title
                                                         action:@selector(onMenuAction:)
                                                  keyEquivalent:@""];
            item.target = gPlaybackTarget;
            item.tag = static_cast<int>(action);
            gPlaybackItemsByTag[static_cast<int32_t>(action)].push_back(item);
        };
        addToggle(@"Repeat", PlaybackMenuAction::ToggleRepeat);
        addToggle(@"Random", PlaybackMenuAction::ToggleRandom);
        addToggle(@"Smooth Transition", PlaybackMenuAction::ToggleSmoothTransition);
        addToggle(@"Show Play Counter", PlaybackMenuAction::ToggleShowPlayCounter);
        return playbackMenu;
    }
}

NSMenu* BuildPotatoMenu(uint32_t potatoEventType) {
    @autoreleasepool {
        if (!gPotatoTarget) {
            gPotatoTarget = [[XmadMenuActionTarget alloc] init];
            gPotatoTarget.eventType = potatoEventType;
        }

        NSMenu* potatoMenu = [[NSMenu alloc] initWithTitle:@"Potato"];
        auto addToggle = [&](NSString* title, PotatoMenuAction action) {
            NSMenuItem* item = [potatoMenu addItemWithTitle:title
                                                       action:@selector(onMenuAction:)
                                                keyEquivalent:@""];
            item.target = gPotatoTarget;
            item.tag = static_cast<int>(action);
            gPotatoItemsByTag[static_cast<int32_t>(action)].push_back(item);
        };
        addToggle(@"30 FPS", PotatoMenuAction::ToggleLowFps);
        addToggle(@"Cheap Visualizer", PotatoMenuAction::ToggleCheapVisualizer);
        // Unlike Cheap Visualizer above (which also forces the main
        // window's analyzer box to the cheap static IdleLogo), this only
        // affects the tray-icon tick - see main.cpp's tray tick gating.
        addToggle(@"Disable Tray Anim", PotatoMenuAction::DisableTrayAnimation);
        // Unlike the toggles above, this one is only read at startup (see
        // CreatePreferredRenderer in main.cpp) - toggling it just persists
        // the preference for next launch, it can't rebuild an already-
        // created SDL_Renderer live.
        addToggle(@"Force Software Rendering (restart)", PotatoMenuAction::ToggleForceSoftware);
        return potatoMenu;
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
        for (NSMenuItem* item : g200Items) item.state = currentPercent == 200 ? NSControlStateValueOn : NSControlStateValueOff;
        for (NSMenuItem* item : g300Items) item.state = currentPercent == 300 ? NSControlStateValueOn : NSControlStateValueOff;
        for (NSMenuItem* item : g400Items) item.state = currentPercent == 400 ? NSControlStateValueOn : NSControlStateValueOff;
    }
}

void InstallOptionsMenu(uint32_t effectsEventType, uint32_t playbackEventType, uint32_t potatoEventType) {
    @autoreleasepool {
        static bool installed = false; // idempotent, same spirit as InstallUiScaleMenu
        if (installed) return;
        installed = true;

        // Top-level "Options" menu (matching the original's real "Option"
        // menu - Source/Menu.frm's mnuXmPButton) holding Effects, Playback
        // and Potato as submenus, rather than adding them directly to the
        // menu bar the way the old flat InstallEffectsMenu did.
        NSMenu* optionsMenu = [[NSMenu alloc] initWithTitle:@"Options"];

        NSMenuItem* effectsItem = [[NSMenuItem alloc] initWithTitle:@"Effects" action:nil keyEquivalent:@""];
        effectsItem.submenu = detail::BuildEffectsMenu(effectsEventType);
        [optionsMenu addItem:effectsItem];

        NSMenuItem* playbackItem = [[NSMenuItem alloc] initWithTitle:@"Playback" action:nil keyEquivalent:@""];
        playbackItem.submenu = detail::BuildPlaybackMenu(playbackEventType);
        [optionsMenu addItem:playbackItem];

        NSMenuItem* potatoItem = [[NSMenuItem alloc] initWithTitle:@"Potato" action:nil keyEquivalent:@""];
        potatoItem.submenu = detail::BuildPotatoMenu(potatoEventType);
        [optionsMenu addItem:potatoItem];

        NSMenuItem* optionsMenuItem = [[NSMenuItem alloc] initWithTitle:@"Options" action:nil keyEquivalent:@""];
        optionsMenuItem.submenu = optionsMenu;
        [[NSApp mainMenu] addItem:optionsMenuItem];
    }
}

void SetEffectsMenuChecked(const EffectsMenuState& state) {
    @autoreleasepool {
        auto apply = [](EffectsMenuAction action, bool on) {
            auto it = gEffectsItemsByTag.find(static_cast<int32_t>(action));
            if (it == gEffectsItemsByTag.end()) return;
            for (NSMenuItem* item : it->second) item.state = on ? NSControlStateValueOn : NSControlStateValueOff;
        };
        apply(EffectsMenuAction::ToggleXSound, state.xSound);
        apply(EffectsMenuAction::ToggleReverb, state.reverb);
        apply(EffectsMenuAction::ToggleSaturation, state.saturation);
        apply(EffectsMenuAction::ToggleCompression, state.compression);
        apply(EffectsMenuAction::ToggleChorus, state.chorus);
    }
}

void SetPlaybackMenuChecked(const PlaybackMenuState& state) {
    @autoreleasepool {
        auto apply = [](PlaybackMenuAction action, bool on) {
            auto it = gPlaybackItemsByTag.find(static_cast<int32_t>(action));
            if (it == gPlaybackItemsByTag.end()) return;
            for (NSMenuItem* item : it->second) item.state = on ? NSControlStateValueOn : NSControlStateValueOff;
        };
        apply(PlaybackMenuAction::ToggleRepeat, state.repeat);
        apply(PlaybackMenuAction::ToggleRandom, state.random);
        apply(PlaybackMenuAction::ToggleSmoothTransition, state.smoothTransition);
        apply(PlaybackMenuAction::ToggleShowPlayCounter, state.showPlayCounter);
    }
}

void SetPotatoMenuChecked(const PotatoMenuState& state) {
    @autoreleasepool {
        auto apply = [](PotatoMenuAction action, bool on) {
            auto it = gPotatoItemsByTag.find(static_cast<int32_t>(action));
            if (it == gPotatoItemsByTag.end()) return;
            for (NSMenuItem* item : it->second) item.state = on ? NSControlStateValueOn : NSControlStateValueOff;
        };
        apply(PotatoMenuAction::ToggleLowFps, state.lowFps);
        apply(PotatoMenuAction::ToggleCheapVisualizer, state.cheapVisualizer);
        apply(PotatoMenuAction::DisableTrayAnimation, state.disableTrayAnim);
        apply(PotatoMenuAction::ToggleForceSoftware, state.forceSoftwareRenderer);
    }
}

void SetMenuTrackingRedrawCallback(std::function<void()> callback) {
    @autoreleasepool {
        gMenuTrackingRedrawCallback = std::move(callback);
        if (gMenuTrackingObserver) return; // idempotent, same spirit as InstallOptionsMenu
        XmadMenuTrackingObserver* observer = [[XmadMenuTrackingObserver alloc] init];
        gMenuTrackingObserver = observer;
        [[NSNotificationCenter defaultCenter] addObserver:observer
                                                  selector:@selector(menuDidBeginTracking:)
                                                      name:NSMenuDidBeginTrackingNotification
                                                    object:nil];
        [[NSNotificationCenter defaultCenter] addObserver:observer
                                                  selector:@selector(menuDidEndTracking:)
                                                      name:NSMenuDidEndTrackingNotification
                                                    object:nil];
    }
}

} // namespace xmad::app
