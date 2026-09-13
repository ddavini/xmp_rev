#pragma once

#include <string>
#include <vector>

// Native "Add to Playlist" file picker (the Eject button's OpenMp3dlg
// equivalent). Split into a pure parsing function and the OS-shelling
// function that feeds it, the same way window_snap.h separates pure
// geometry from live SDL state - ParseDialogOutput is what a test can
// actually reach; OpenNativeFileDialog (which pops a real native dialog
// and blocks on it) is not something a headless test can safely drive.

namespace xmad::app {

// Splits raw stdout from a file-picker helper into individual paths.
// Handles all three shapes this project's dialog helpers can produce:
// zenity's default '|'-separated single line, and the newline-per-path
// output that both the osascript (macOS) and `kdialog --separate-output`
// branches emit. Blank entries (trailing separators, empty input) are
// dropped; \r is stripped so it tolerates either line-ending convention.
std::vector<std::string> ParseDialogOutput(const std::string& raw);

// Opens a native file-selection dialog (Cocoa panel via osascript on
// macOS; zenity, falling back to kdialog, on Linux) and returns the
// chosen paths, or an empty vector if the user cancelled or no dialog
// helper is available. Blocks until the dialog is dismissed.
std::vector<std::string> OpenNativeFileDialog();

} // namespace xmad::app
