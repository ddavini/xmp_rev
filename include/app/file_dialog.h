#pragma once

#include <optional>
#include <string>
#include <vector>

// Native "Add to Playlist" file/folder pickers (the Eject button's
// OpenMp3dlg equivalent), plus the Playlist window's "Save As..."
// picker. Split into pure parsing functions and the OS-shelling
// functions that feed them, the same way window_snap.h separates pure
// geometry from live SDL state - ParseDialogOutput/TrimTrailingNewline
// are what a test can actually reach; the OpenNativeFileDialog* /
// SaveNativeFileDialog functions (which pop a real native dialog and
// block on it) are not something a headless test can safely drive.

namespace xmad::app {

// Splits raw stdout from a file-picker helper into individual paths.
// Handles all three shapes this project's dialog helpers can produce:
// zenity's default '|'-separated single line, and the newline-per-path
// output that both the osascript (macOS) and `kdialog --separate-output`
// branches emit. Blank entries (trailing separators, empty input) are
// dropped; \r is stripped so it tolerates either line-ending convention.
std::vector<std::string> ParseDialogOutput(const std::string& raw);

// Opens a native file-selection dialog, restricted to mp3/flac/m3u
// (Cocoa panel via osascript on macOS; zenity, falling back to kdialog,
// on Linux), and returns the chosen paths, or an empty vector if the
// user cancelled or no dialog helper is available. Blocks until the
// dialog is dismissed.
std::vector<std::string> OpenNativeFileDialogFiles();

// Same as OpenNativeFileDialogFiles but for picking one or more whole
// folders instead (AppleScript's "choose folder" on macOS; zenity's
// --file-selection --directory, falling back to kdialog --getexistingdirectory,
// on Linux - each OS's own separate directory-picker mode, since none of
// these dialog helpers can combine file- and folder-selection in one
// dialog). The caller is expected to expand a chosen folder's contents
// itself (see main.cpp's handleDroppedFile) - this just returns the
// folder path(s) picked, same shape as OpenNativeFileDialogFiles.
std::vector<std::string> OpenNativeFileDialogFolder();

// Trims trailing CR/LF - the single-path analogue of ParseDialogOutput
// (a save dialog only ever returns one path).
std::string TrimTrailingNewline(const std::string& raw);

// Opens a native "Save As" dialog (Cocoa panel via osascript on macOS;
// zenity --file-selection --save, falling back to kdialog
// --getsavefilename, on Linux) pre-filled with defaultName. Returns the
// chosen path, or std::nullopt if the user cancelled or no dialog helper
// is available. Blocks until dismissed, same as OpenNativeFileDialog.
std::optional<std::string> SaveNativeFileDialog(const std::string& defaultName);

} // namespace xmad::app
