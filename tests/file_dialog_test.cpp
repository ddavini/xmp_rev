// Covers ParseDialogOutput, the pure part of the Eject-button file picker
// - the live OpenNativeFileDialog() actually pops a native OS dialog and
// blocks on it, which a headless test can't safely drive (see
// app/file_dialog.h). Also covers EscapeForAppleScriptString/
// EscapeForShellSingleQuoted (security review: SaveNativeFileDialog's
// command-string escaping) - unlike the live dialogs, a non-interactive
// shell invocation (printf via popen) is safe and fast to actually run,
// so that one gets a real end-to-end round-trip check, not just a
// string-equality check against the escaping logic in isolation.

#include "app/file_dialog.h"

#include <array>
#include <cstdio>

using xmad::app::EscapeForAppleScriptString;
using xmad::app::EscapeForShellSingleQuoted;
using xmad::app::ParseDialogOutput;
using xmad::app::TrimTrailingNewline;

namespace {
int g_failures = 0;

void Check(bool cond, const char* what) {
    if (!cond) {
        std::printf("FAIL: %s\n", what);
        ++g_failures;
    }
}
} // namespace

int main() {
    // osascript / kdialog --separate-output shape: one path per line.
    {
        auto r = ParseDialogOutput("/music/a.mp3\n/music/b.flac\n");
        Check(r.size() == 2, "newline-separated: two paths");
        Check(r.size() == 2 && r[0] == "/music/a.mp3", "newline-separated: first path exact");
        Check(r.size() == 2 && r[1] == "/music/b.flac", "newline-separated: second path exact");
    }

    // zenity's default multi-select shape: '|'-separated, single line.
    {
        auto r = ParseDialogOutput("/music/a.mp3|/music/b.flac|/music/c.mp3");
        Check(r.size() == 3, "pipe-separated: three paths");
        Check(r.size() == 3 && r[2] == "/music/c.mp3", "pipe-separated: last path exact");
    }

    // A single selection, no trailing separator at all.
    {
        auto r = ParseDialogOutput("/music/only.mp3");
        Check(r.size() == 1 && r[0] == "/music/only.mp3", "single path, no trailing separator");
    }

    // Cancelled dialog / no dialog helper installed: empty stdout.
    {
        auto r = ParseDialogOutput("");
        Check(r.empty(), "empty input yields no paths");
    }

    // CRLF line endings (kdialog on some setups) don't leave stray \r's
    // stuck to path ends.
    {
        auto r = ParseDialogOutput("/music/a.mp3\r\n/music/b.flac\r\n");
        Check(r.size() == 2 && r[0] == "/music/a.mp3", "CRLF: \\r stripped from first path");
        Check(r.size() == 2 && r[1] == "/music/b.flac", "CRLF: \\r stripped from second path");
    }

    // TrimTrailingNewline - the single-path analogue used by
    // SaveNativeFileDialog (which pops a live dialog, so not testable here).
    {
        Check(TrimTrailingNewline("/music/out.m3u\n") == "/music/out.m3u", "trim: trailing LF");
        Check(TrimTrailingNewline("/music/out.m3u\r\n") == "/music/out.m3u", "trim: trailing CRLF");
        Check(TrimTrailingNewline("/music/out.m3u") == "/music/out.m3u", "trim: no trailing newline, unchanged");
        Check(TrimTrailingNewline("") == "", "trim: empty input stays empty");
    }

    // Security review: EscapeForAppleScriptString defends
    // SaveNativeFileDialog's macOS branch against a defaultName that
    // breaks out of the surrounding double-quoted AppleScript string.
    {
        Check(EscapeForAppleScriptString("plain") == "plain", "applescript escape: no special chars, unchanged");
        Check(EscapeForAppleScriptString("a\"b") == "a\\\"b", "applescript escape: embedded quote escaped");
        Check(EscapeForAppleScriptString("a\\b") == "a\\\\b", "applescript escape: embedded backslash escaped");
        Check(EscapeForAppleScriptString("\"; do shell script \"rm -rf ~") == "\\\"; do shell script \\\"rm -rf ~",
              "applescript escape: a quote-breakout attempt is neutralized");
    }

    // Security review: EscapeForShellSingleQuoted defends the Linux
    // branch (zenity/kdialog, invoked via popen -> /bin/sh -c) the same
    // way. The last case actually round-trips the escaped result through
    // a real non-interactive shell (printf via popen) to confirm nothing
    // in a crafted string is ever interpreted as a separate command.
    {
        Check(EscapeForShellSingleQuoted("plain") == "plain", "shell escape: no special chars, unchanged");
        Check(EscapeForShellSingleQuoted("a'b") == "a'\\''b", "shell escape: embedded single quote escaped");
        Check(EscapeForShellSingleQuoted("'; rm -rf ~; '") == "'\\''; rm -rf ~; '\\''",
              "shell escape: a quote-breakout attempt is neutralized");

        const std::string malicious = "'; touch /tmp/xmad_test_should_never_run; echo 'x";
        const std::string cmd = "printf '%s' '" + EscapeForShellSingleQuoted(malicious) + "'";
        std::string out;
        if (FILE* pipe = popen(cmd.c_str(), "r")) {
            std::array<char, 256> buf{};
            while (fgets(buf.data(), buf.size(), pipe) != nullptr) out += buf.data();
            pclose(pipe);
        }
        Check(out == malicious, "shell escape: round-trips through a real shell unchanged, nothing executed");
    }

    if (g_failures == 0) {
        std::printf("PASS\n");
        return 0;
    }
    std::printf("%d FAILURE(S)\n", g_failures);
    return 1;
}
