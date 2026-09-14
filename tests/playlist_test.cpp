// Covers Playlist::LoadM3U's relative-path resolution - relative entries
// are resolved against the M3U file's own directory, not the process's
// cwd. This is the exact bug reported against a real playlist.m3u on a
// real machine: bare filenames next to their own tracks (the normal case
// for a playlist saved alongside its music), which previously only
// "worked" by coincidence when cwd already happened to be that directory.
// See playlist.h's own comment on LoadM3U.

#include "app/playlist.h"

#include <cstdio>

using xmad::app::Playlist;

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
    // Bare filenames (no path at all) resolve against the M3U file's own
    // directory, not wherever this test binary happens to run from.
    {
        Playlist pl;
        const bool ok = pl.LoadM3U("tests/fixtures/relative.m3u");
        Check(ok, "relative.m3u: load succeeds");
        Check(pl.size() == 2, "relative.m3u: two tracks (comment/blank line skipped)");
        Check(pl.size() == 2 && pl.at(0) == "tests/fixtures/track_a.mp3",
              "relative.m3u: first entry resolved against the m3u's own directory");
        Check(pl.size() == 2 && pl.at(1) == "tests/fixtures/track_b.mp3",
              "relative.m3u: second entry resolved against the m3u's own directory");
    }

    // An already-absolute entry passes through unchanged, not prefixed
    // with the m3u's own directory.
    {
        Playlist pl;
        const bool ok = pl.LoadM3U("tests/fixtures/absolute.m3u");
        Check(ok, "absolute.m3u: load succeeds");
        Check(pl.size() == 1 && pl.at(0) == "/nonexistent/absolute/track.mp3",
              "absolute.m3u: absolute entry left untouched");
    }

    if (g_failures == 0) {
        std::printf("PASS\n");
        return 0;
    }
    std::printf("%d FAILURE(S)\n", g_failures);
    return 1;
}
