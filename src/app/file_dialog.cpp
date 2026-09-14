#include "app/file_dialog.h"

#include <array>
#include <cstdio>

namespace xmad::app {

std::vector<std::string> ParseDialogOutput(const std::string& raw) {
    std::vector<std::string> paths;
    std::string cur;
    for (char c : raw) {
        if (c == '\n' || c == '|') {
            if (!cur.empty()) paths.push_back(cur);
            cur.clear();
        } else if (c != '\r') {
            cur += c;
        }
    }
    if (!cur.empty()) paths.push_back(cur);
    return paths;
}

namespace {

// Shared by every OS-shelling function below - runs `cmd`, reads all of
// its stdout, returns it raw for the caller to parse.
std::string RunPipedCommand(const char* cmd) {
    FILE* pipe = popen(cmd, "r");
    if (!pipe) return "";
    std::string all;
    std::array<char, 4096> buf{};
    while (fgets(buf.data(), buf.size(), pipe) != nullptr) all += buf.data();
    pclose(pipe);
    return all;
}

} // namespace

std::vector<std::string> OpenNativeFileDialogFiles() {
#if defined(__APPLE__)
    // osascript pops a real Cocoa NSOpenPanel without linking AppKit
    // directly or adding an Objective-C++ translation unit to the build.
    const char* cmd =
        "osascript -e 'try' "
        "-e 'set theFiles to choose file with prompt \"Add to Playlist\" "
        "of type {\"mp3\",\"flac\",\"m3u\"} with multiple selections allowed' "
        "-e 'set out to \"\"' "
        "-e 'repeat with f in theFiles' "
        "-e 'set out to out & (POSIX path of f) & linefeed' "
        "-e 'end repeat' "
        "-e 'return out' "
        "-e 'on error' "
        "-e 'return \"\"' "
        "-e 'end try' 2>/dev/null";
#else
    // zenity is present with nearly every GTK-based desktop's file-manager
    // stack; kdialog covers KDE-only systems that lack it.
    const char* cmd =
        "zenity --file-selection --multiple --title='Add to Playlist' "
        "--file-filter='Audio/Playlist (mp3, flac, m3u) | *.mp3 *.flac *.m3u' 2>/dev/null "
        "|| kdialog --getopenfilename --multiple --separate-output . "
        "'Audio/Playlist files (*.mp3 *.flac *.m3u)' 2>/dev/null";
#endif
    return ParseDialogOutput(RunPipedCommand(cmd));
}

std::vector<std::string> OpenNativeFileDialogFolder() {
#if defined(__APPLE__)
    // "choose folder" is AppleScript's dedicated command for this - not
    // "choose file or folder" (not a real command; confirmed against
    // StandardAdditions.sdef, which has "choose file" and "choose
    // folder" as entirely separate commands, neither combinable with the
    // other's parameters - "choose folder" has no file-type filter to
    // begin with, so there was never a way to fold this into
    // OpenNativeFileDialogFiles as one dialog).
    const char* cmd =
        "osascript -e 'try' "
        "-e 'set theFolders to choose folder with prompt \"Add to Playlist\" with multiple selections allowed' "
        "-e 'set out to \"\"' "
        "-e 'repeat with f in theFolders' "
        "-e 'set out to out & (POSIX path of f) & linefeed' "
        "-e 'end repeat' "
        "-e 'return out' "
        "-e 'on error' "
        "-e 'return \"\"' "
        "-e 'end try' 2>/dev/null";
#else
    const char* cmd = "zenity --file-selection --directory --multiple --title='Add Folder to Playlist' 2>/dev/null "
                       "|| kdialog --getexistingdirectory . 2>/dev/null";
#endif
    return ParseDialogOutput(RunPipedCommand(cmd));
}

std::string TrimTrailingNewline(const std::string& raw) {
    std::string s = raw;
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
    return s;
}

std::optional<std::string> SaveNativeFileDialog(const std::string& defaultName) {
#if defined(__APPLE__)
    // Same osascript-without-linking-AppKit approach as OpenNativeFileDialog.
    const std::string cmd =
        "osascript -e 'try' "
        "-e 'set theFile to choose file name with prompt \"Save Playlist\" default name \"" +
        defaultName +
        "\"' "
        "-e 'return POSIX path of theFile' "
        "-e 'on error' "
        "-e 'return \"\"' "
        "-e 'end try' 2>/dev/null";
#else
    const std::string cmd = "zenity --file-selection --save --confirm-overwrite --title='Save Playlist' "
                             "--filename='" +
                             defaultName +
                             "' 2>/dev/null "
                             "|| kdialog --getsavefilename ./" +
                             defaultName + " 'Playlist (*.m3u)' 2>/dev/null";
#endif
    const std::string path = TrimTrailingNewline(RunPipedCommand(cmd.c_str()));
    return path.empty() ? std::nullopt : std::optional<std::string>(path);
}

} // namespace xmad::app
