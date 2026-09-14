#include "app/media_remote.h"

#import <Cocoa/Cocoa.h>
#import <MediaPlayer/MediaPlayer.h>
#include <SDL2/SDL.h>

namespace {
uint32_t gRemoteCommandEventType = 0;
bool gCommandsEnabled = false;

void PushCommand(xmad::app::MediaRemoteCommand cmd) {
    SDL_Event ev;
    SDL_zero(ev);
    ev.type = gRemoteCommandEventType;
    ev.user.code = static_cast<Sint32>(cmd);
    SDL_PushEvent(&ev);
}
} // namespace

namespace xmad::app {

void EnableMediaRemoteCommands(uint32_t remoteCommandEventType) {
    @autoreleasepool {
        if (gCommandsEnabled) return;
        gCommandsEnabled = true;
        gRemoteCommandEventType = remoteCommandEventType;

        MPRemoteCommandCenter* center = [MPRemoteCommandCenter sharedCommandCenter];
        [center.togglePlayPauseCommand addTargetWithHandler:^MPRemoteCommandHandlerStatus(
                                            MPRemoteCommandEvent* _Nonnull) {
          PushCommand(MediaRemoteCommand::Pause);
          return MPRemoteCommandHandlerStatusSuccess;
        }];
        [center.playCommand addTargetWithHandler:^MPRemoteCommandHandlerStatus(MPRemoteCommandEvent* _Nonnull) {
          PushCommand(MediaRemoteCommand::Play);
          return MPRemoteCommandHandlerStatusSuccess;
        }];
        [center.pauseCommand addTargetWithHandler:^MPRemoteCommandHandlerStatus(MPRemoteCommandEvent* _Nonnull) {
          PushCommand(MediaRemoteCommand::Pause);
          return MPRemoteCommandHandlerStatusSuccess;
        }];
        [center.nextTrackCommand addTargetWithHandler:^MPRemoteCommandHandlerStatus(MPRemoteCommandEvent* _Nonnull) {
          PushCommand(MediaRemoteCommand::Next);
          return MPRemoteCommandHandlerStatusSuccess;
        }];
        [center.previousTrackCommand
            addTargetWithHandler:^MPRemoteCommandHandlerStatus(MPRemoteCommandEvent* _Nonnull) {
              PushCommand(MediaRemoteCommand::Previous);
              return MPRemoteCommandHandlerStatusSuccess;
            }];

        // Establishes a defined baseline state before any track has ever
        // played, so the first real UpdateNowPlayingInfo call (on
        // openPlaylistIndex) is a genuine .stopped -> .playing
        // transition rather than a cold first-ever set from an undefined
        // prior state - which macOS appears not to register as actually
        // claiming "current Now Playing app" status (observed symptom:
        // the very first Bluetooth play press launched Apple Music
        // instead of reaching this app, but it worked immediately after
        // any later real pause/resume transition here).
        if (@available(macOS 10.13.1, *)) {
            [MPNowPlayingInfoCenter defaultCenter].playbackState = MPNowPlayingPlaybackStateStopped;
        }
    }
}

void UpdateNowPlayingInfo(const std::string& title, double durationSeconds, double positionSeconds,
                           bool isPlaying) {
    @autoreleasepool {
        NSMutableDictionary* info = [NSMutableDictionary dictionary];
        info[MPMediaItemPropertyTitle] = [NSString stringWithUTF8String:title.c_str()];
        info[MPMediaItemPropertyPlaybackDuration] = @(durationSeconds);
        info[MPNowPlayingInfoPropertyElapsedPlaybackTime] = @(positionSeconds);
        info[MPNowPlayingInfoPropertyPlaybackRate] = @(isPlaying ? 1.0 : 0.0);
        MPNowPlayingInfoCenter* center = [MPNowPlayingInfoCenter defaultCenter];
        center.nowPlayingInfo = info;
        // The info dictionary's playback-rate key alone isn't what macOS
        // uses to decide which app currently owns "Now Playing" status
        // (and therefore gets Bluetooth/media-key commands routed to it)
        // - this separate property is. Missing it is consistent with the
        // observed symptom: commands worked right after a state change
        // (when nowPlayingInfo was freshly touched) and stopped shortly
        // after, as if the app kept quietly losing that status.
        if (@available(macOS 10.13.1, *)) {
            center.playbackState = isPlaying ? MPNowPlayingPlaybackStatePlaying : MPNowPlayingPlaybackStatePaused;
        }
    }
}

void ClearNowPlayingInfo() {
    @autoreleasepool {
        MPNowPlayingInfoCenter* center = [MPNowPlayingInfoCenter defaultCenter];
        center.nowPlayingInfo = nil;
        if (@available(macOS 10.13.1, *)) {
            center.playbackState = MPNowPlayingPlaybackStateStopped;
        }
    }
}

} // namespace xmad::app
