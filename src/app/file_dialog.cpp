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

std::vector<std::string> OpenNativeFileDialog() {
#if defined(__APPLE__)
    // osascript pops a real Cocoa NSOpenPanel without linking AppKit
    // directly or adding an Objective-C++ translation unit to the build.
    const char* cmd =
        "osascript -e 'try' "
        "-e 'set theFiles to choose file with prompt \"Add to Playlist\" "
        "of type {\"mp3\",\"flac\"} with multiple selections allowed' "
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
        "--file-filter='Audio (mp3, flac) | *.mp3 *.flac' 2>/dev/null "
        "|| kdialog --getopenfilename --multiple --separate-output . "
        "'Audio files (*.mp3 *.flac)' 2>/dev/null";
#endif
    FILE* pipe = popen(cmd, "r");
    if (!pipe) return {};
    std::string all;
    std::array<char, 4096> buf{};
    while (fgets(buf.data(), buf.size(), pipe) != nullptr) all += buf.data();
    pclose(pipe);
    return ParseDialogOutput(all);
}

} // namespace xmad::app
