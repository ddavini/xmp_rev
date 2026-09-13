// Verifies the magnetic-docking geometry directly - real drag testing needs
// live OS cursor position (SDL_GetGlobalMouseState), which can't be driven
// through synthetic events, so this is the part of that feature that's
// actually reachable by a test.

#include "app/window_snap.h"

#include <algorithm>
#include <cstdio>

using xmad::app::FindDockedGroup;
using xmad::app::IsDocked;
using xmad::app::SnapDragPosition;
using xmad::app::WinRect;

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
    const int kThreshold = 14;

    // Dock below: dragged window's top edge close to other's bottom edge.
    {
        WinRect other{100, 100, 330, 155}; // main window
        auto r = SnapDragPosition(105, 258, 330, 130, {other}, kThreshold); // 3px off from flush-below (255)
        Check(r.y == 255, "dock-below snaps y to other's bottom edge");
    }

    // Dock above.
    {
        WinRect other{100, 300, 330, 130};
        auto r = SnapDragPosition(102, 166, 330, 130, {other}, kThreshold); // 4px off from flush-above (170)
        Check(r.y == 170, "dock-above snaps y to (other.y - dragged.h)");
    }

    // Dock right-of.
    {
        WinRect other{0, 0, 330, 155};
        auto r = SnapDragPosition(340, 10, 200, 100, {other}, kThreshold); // 10px off from flush (330)
        Check(r.x == 330, "dock-right snaps x to other's right edge");
    }

    // Left-edge alignment (same column, not adjacent).
    {
        WinRect other{50, 0, 330, 155};
        auto r = SnapDragPosition(58, 400, 330, 130, {other}, kThreshold); // 8px off from 50
        Check(r.x == 50, "left-edge alignment snaps x to other's x");
    }

    // No snap when far outside the threshold.
    {
        WinRect other{0, 0, 330, 155};
        auto r = SnapDragPosition(500, 500, 330, 130, {other}, kThreshold);
        Check(r.x == 500 && r.y == 500, "no snap when far from every other window");
    }

    // Multiple others: snaps independently against each candidate edge.
    {
        WinRect mainW{0, 0, 330, 155};
        WinRect eqW{0, 155, 330, 130};
        // Dropped near eq's bottom edge (155+130=285) -> should dock below eq.
        auto r = SnapDragPosition(4, 290, 330, 140, {mainW, eqW}, kThreshold);
        Check(r.y == 285, "snaps against the correct one of several candidates");
    }

    // IsDocked: flush-below with overlapping x range counts as docked.
    {
        WinRect main{0, 0, 330, 155};
        WinRect eq{0, 155, 330, 130};
        Check(IsDocked(main, eq, 2), "flush-below rects are docked");
    }

    // IsDocked: same y-flush edge but zero x-overlap is NOT docked (not
    // actually touching, just coincidentally aligned far apart).
    {
        WinRect main{0, 0, 330, 155};
        WinRect far{1000, 155, 330, 130};
        Check(!IsDocked(main, far, 2), "flush edge with no perpendicular overlap is not docked");
    }

    // IsDocked: merely close (within snap-drag threshold) but not flush is
    // NOT docked under a tight tolerance - only actually-snapped windows
    // count, not ones that are just nearby mid-drag.
    {
        WinRect main{0, 0, 330, 155};
        WinRect near{0, 165, 330, 130}; // 10px gap
        Check(!IsDocked(main, near, 2), "merely nearby (not flush) is not docked under a tight tolerance");
    }

    // FindDockedGroup: a 3-window vertical stack (main -> eq -> playlist)
    // all report as one transitively-docked group from the root.
    {
        std::vector<WinRect> all = {
            {0, 0, 330, 155},   // 0: main
            {0, 155, 330, 130}, // 1: eq, flush below main
            {0, 285, 330, 140}, // 2: playlist, flush below eq (not touching main directly)
        };
        auto group = FindDockedGroup(0, all, 2);
        Check(group.size() == 2, "3-window stack: whole chain reachable from root");
        Check(std::find(group.begin(), group.end(), 1) != group.end(), "eq is in the docked group");
        Check(std::find(group.begin(), group.end(), 2) != group.end(),
              "playlist is in the docked group via the chain through eq");
    }

    // FindDockedGroup: playlist detached (dragged away) - only eq follows.
    {
        std::vector<WinRect> all = {
            {0, 0, 330, 155},     // 0: main
            {0, 155, 330, 130},   // 1: eq, still flush below main
            {900, 900, 330, 140}, // 2: playlist, dragged far away
        };
        auto group = FindDockedGroup(0, all, 2);
        Check(group.size() == 1, "detached playlist does not join the group");
        Check(std::find(group.begin(), group.end(), 1) != group.end(), "eq still follows main");
        Check(std::find(group.begin(), group.end(), 2) == group.end(), "detached playlist is excluded");
    }

    if (g_failures == 0) {
        std::printf("PASS\n");
        return 0;
    }
    std::printf("%d FAILURE(S)\n", g_failures);
    return 1;
}
