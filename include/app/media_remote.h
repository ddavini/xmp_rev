#pragma once

#include <cstdint>
#include <string>

// macOS-only: registers MPRemoteCommandCenter handlers so play/pause/
// next/previous presses from Bluetooth headphones, AirPods, and
// car-stereo controls reach the app, and publishes Now Playing info
// (title/duration/position) to Control Center and the lock screen via
// MPNowPlayingInfoCenter so those surfaces show the current track at
// all. Implemented in src/app/media_remote.mm (Objective-C++), same
// approach as menu_bar_icon.h - the only other place in the codebase
// that links AppKit/system frameworks directly.
//
// Only ever included under #ifdef __APPLE__ in main.cpp, and only ever
// compiled on Darwin (see Makefile) - no stub/no-op implementation
// exists or is needed for other platforms.

namespace xmad::app {

// What a remote-command press maps to, carried as `ev.user.code` on an
// event of type `remoteCommandEventType` (an SDL_RegisterEvents-allocated
// code owned by the caller) - same custom-event bridging pattern as
// menu_bar_icon.h's ShowMenuBarIcon, since MPRemoteCommandCenter's
// handler blocks fire on whatever thread/queue the system chooses, not
// necessarily the SDL main loop's thread.
enum class MediaRemoteCommand : int32_t { Play, Pause, Next, Previous };

// Registers MPRemoteCommandCenter handlers for play/pause/next/previous.
// A single physical play/pause button (most Bluetooth headphones and car
// stereos) sends the combined toggle command rather than separate
// play/pause presses, so it's mapped to Pause here - the caller's
// existing Pause handling already toggles. Idempotent - a second call is
// a no-op. Call once at startup.
void EnableMediaRemoteCommands(uint32_t remoteCommandEventType);

// Updates MPNowPlayingInfoCenter with the current track, called whenever
// the open track or its play/pause state changes (not on a timer -
// MPNowPlayingInfoCenter interpolates the scrubber between calls itself
// from `isPlaying`'s rate). `title` is whatever the caller already shows
// elsewhere (ID3 title, or filename if there isn't one).
void UpdateNowPlayingInfo(const std::string& title, double durationSeconds, double positionSeconds,
                           bool isPlaying);

// Clears Now Playing info (e.g. on Stop) so Control Center/the lock
// screen stop showing a track that's no longer active.
void ClearNowPlayingInfo();

} // namespace xmad::app
