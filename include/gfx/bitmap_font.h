#pragma once

#include <string_view>

#include "gfx/image.h"

// Direct port of Source/xmDisplay.ctl's DrawChar/PaintChar: a 5x6px glyph
// atlas (Source/Img_new/DISPLAY.bmp) laid out as 30 columns x (3 rows x 4
// color variants). The live app always renders with Mode.C (the 3rd color
// variant, hardcoded in xCaption's `Call DrawChar(..., C)`), which is a
// green-on-black block at y-offset 19*2=38px - the other three color
// variants in the sheet are unused dead weight, so only that block is
// loaded.
//
// Two faithfully-preserved quirks from the original (not "fixed" here,
// since the brief is pixel-identical behavior):
//  - '@' draws two glyph cells (at the character's position and position+1)
//    but the loop only advances by one input character, so a character
//    immediately following '@' overwrites the second half.
//  - ' ' (space) draws its blank glyph at position+1, not at its own
//    position - functionally invisible because Picture1.Cls() already
//    blanked position, and position+1 normally gets overwritten by the
//    next real character anyway. Only visible for a trailing space.
//  - The VB source has a duplicate `Case "&"` (second one unreachable) and
//    a `Case "..."` that can never match a single character (dead code) -
//    neither is ported since they have no observable effect.

namespace xmad::gfx {

class BitmapFont {
public:
    explicit BitmapFont(const Image& atlas);

    // Renders `text` into `dest` starting at (x, y), scaled by `scale`
    // (native cell is 5x6px; the shipped app only ever used scale 1, i.e.
    // xmDisplay.Size = 5). If `fieldWidth` > 0, centers the string within
    // that pixel width, matching xmDisplay's Center property.
    void DrawText(Image& dest, int x, int y, std::string_view text, int scale = 1,
                  int fieldWidth = 0) const;

    static constexpr int kCellW = 5;
    static constexpr int kCellH = 6;

private:
    struct Cell { int col; int row; };
    static Cell CharToCell(char upper);

    void BlitCell(Image& dest, int destX, int destY, Cell cell, int scale) const;

    const Image& atlas_;
    static constexpr int kColorBlockHeight = 19;
    static constexpr int kLiveColorIndex = 2; // Mode.C
};

} // namespace xmad::gfx
