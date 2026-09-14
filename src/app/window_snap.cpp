#include "app/window_snap.h"

#include <cstdlib>

namespace xmad::app {

WinRect SnapDragPosition(int x, int y, int w, int h, const std::vector<WinRect>& others, int threshold) {
    for (const auto& o : others) {
        if (std::abs(y - (o.y + o.h)) <= threshold) y = o.y + o.h;    // dock below
        if (std::abs((y + h) - o.y) <= threshold) y = o.y - h;        // dock above
        if (std::abs(x - (o.x + o.w)) <= threshold) x = o.x + o.w;    // dock right of
        if (std::abs((x + w) - o.x) <= threshold) x = o.x - w;        // dock left of
        if (std::abs(x - o.x) <= threshold) x = o.x;                  // align left edges
        if (std::abs(y - o.y) <= threshold) y = o.y;                  // align top edges
    }
    return {x, y, w, h};
}

bool IsDocked(const WinRect& a, const WinRect& b, int tolerance) {
    const bool xOverlap = a.x < b.x + b.w && b.x < a.x + a.w;
    const bool yOverlap = a.y < b.y + b.h && b.y < a.y + a.h;
    const bool vertFlush =
        xOverlap && (std::abs(a.y - (b.y + b.h)) <= tolerance || std::abs((a.y + a.h) - b.y) <= tolerance);
    const bool horizFlush =
        yOverlap && (std::abs(a.x - (b.x + b.w)) <= tolerance || std::abs((a.x + a.w) - b.x) <= tolerance);
    return vertFlush || horizFlush;
}

std::vector<int> FindDockedGroup(int rootIndex, const std::vector<WinRect>& all, int tolerance) {
    std::vector<int> group;
    std::vector<bool> visited(all.size(), false);
    visited[rootIndex] = true;
    std::vector<int> frontier{rootIndex};
    while (!frontier.empty()) {
        std::vector<int> next;
        for (int i : frontier) {
            for (int j = 0; j < static_cast<int>(all.size()); ++j) {
                if (visited[j]) continue;
                if (IsDocked(all[i], all[j], tolerance)) {
                    visited[j] = true;
                    group.push_back(j);
                    next.push_back(j);
                }
            }
        }
        frontier = std::move(next);
    }
    return group;
}

WinRect SnapToScreenEdges(int x, int y, int w, int h, const WinRect& screen, int threshold) {
    if (std::abs(x - screen.x) <= threshold) x = screen.x;                                   // left
    if (std::abs((x + w) - (screen.x + screen.w)) <= threshold) x = screen.x + screen.w - w;  // right
    if (std::abs(y - screen.y) <= threshold) y = screen.y;                                    // top
    if (std::abs((y + h) - (screen.y + screen.h)) <= threshold) y = screen.y + screen.h - h;  // bottom
    return {x, y, w, h};
}

} // namespace xmad::app
