#pragma once

#include <vector>

// Pure geometry for magnetic window docking - deliberately has no SDL
// dependency so it's testable without a real window/display (dragging
// itself reads live global mouse state via SDL_GetGlobalMouseState, which
// can't be faked through synthetic events, so this is the part of the
// feature that verification can actually reach).

namespace xmad::app {

struct WinRect {
    int x, y, w, h;
};

// If an edge of the rect at (x,y,w,h) is within `threshold` px of a
// matching edge on any rect in `others`, that edge snaps flush against it
// (dock above/below, dock left/right-of, or plain top/left alignment).
// Returns the possibly-adjusted top-left position.
WinRect SnapDragPosition(int x, int y, int w, int h, const std::vector<WinRect>& others, int threshold);

// True if `a` and `b` share a flush edge (within `tolerance` px) with their
// perpendicular extents overlapping - i.e. they look physically docked,
// not just coincidentally nearby. Use a tight tolerance here (unlike the
// much looser `threshold` SnapDragPosition uses to decide when to *start*
// snapping) since a window that has actually snapped sits exactly flush.
bool IsDocked(const WinRect& a, const WinRect& b, int tolerance);

// Indices into `all` (excluding rootIndex itself) that are docked to the
// window at rootIndex, directly or transitively through a chain of other
// docked windows - the set that should move rigidly together when the
// root window is dragged.
std::vector<int> FindDockedGroup(int rootIndex, const std::vector<WinRect>& all, int tolerance);

} // namespace xmad::app
