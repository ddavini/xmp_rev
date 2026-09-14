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
        [MPNowPlayingInfoCenter defaultCenter].nowPlayingInfo = info;
    }
}

void ClearNowPlayingInfo() {
    @autoreleasepool {
        [MPNowPlayingInfoCenter defaultCenter].nowPlayingInfo = nil;
    }
}

} // namespace xmad::app
