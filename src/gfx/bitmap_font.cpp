#include "gfx/bitmap_font.h"

#include <algorithm>
#include <cctype>

namespace xmad::gfx {

BitmapFont::BitmapFont(const Image& atlas) : atlas_(atlas) {}

BitmapFont::Cell BitmapFont::CharToCell(char c) {
    // Mirrors xmDisplay.ctl's DrawChar Select Case exactly (row, col).
    if (c >= '0' && c <= '9') return {c - '0', 1};
    switch (c) {
        case '(': return {13, 1};
        case ')': return {14, 1};
        case '"': return {26, 0};
        // '@' and ' ' are handled specially in DrawText (two-cell / shifted
        // draw) - CharToCell is never asked for them.
        case ':': return {12, 1};
        case '-': return {15, 1};
        case '\'': return {16, 1};
        case '!': return {17, 1};
        case '_': return {18, 1};
        case '+': return {19, 1};
        case '\\': return {20, 1};
        case '/': return {21, 1};
        case '[': return {22, 1};
        case ']': return {23, 1};
        case '^': return {24, 1};
        case '&': return {25, 1};
        case '.': return {27, 1};
        case '=': return {28, 1};
        case '$': return {29, 1};
        case '?': return {3, 2};
        case '*': return {4, 2};
        default:
            if (c >= 'A' && c <= 'Z') return {c - 'A', 0};
            return {11, 1}; // Case Else placeholder glyph
    }
}

void BitmapFont::BlitCell(Image& dest, int destX, int destY, Cell cell, int scale) const {
    const int srcX = cell.col * kCellW;
    const int srcY = kColorBlockHeight * kLiveColorIndex + cell.row * kCellH;

    for (int sy = 0; sy < kCellH; ++sy) {
        if (srcY + sy < 0 || srcY + sy >= atlas_.height) continue;
        for (int sx = 0; sx < kCellW; ++sx) {
            if (srcX + sx < 0 || srcX + sx >= atlas_.width) continue;
            const uint8_t* src = atlas_.pixel(srcX + sx, srcY + sy);
            // The atlas's "off" pixels are pure black (0,0,0) baked into
            // the source BMP (which has no alpha channel at all - see
            // LoadBMP). Treating that exact color as a transparent color
            // key - skip the copy, leave dest untouched - lets a caller
            // draw this text over a non-black fill (e.g. the PL/EQ toggle
            // buttons' green background) without every glyph cell
            // punching an opaque black hole through it; TODO: "the PL and
            // EQ buttons are ugly ... uniform ... green instead of black
            // where the letters are".
            if (src[0] == 0 && src[1] == 0 && src[2] == 0) continue;
            for (int dy = 0; dy < scale; ++dy) {
                const int py = destY + sy * scale + dy;
                if (py < 0 || py >= dest.height) continue;
                for (int dx = 0; dx < scale; ++dx) {
                    const int px = destX + sx * scale + dx;
                    if (px < 0 || px >= dest.width) continue;
                    std::copy_n(src, 4, dest.pixel(px, py));
                }
            }
        }
    }
}

void BitmapFont::DrawText(Image& dest, int x, int y, std::string_view text, int scale,
                           int fieldWidth) const {
    const int cellPitch = kCellW * scale;
    int x0 = x;
    if (fieldWidth > 0) {
        // Matches PaintChar: (ScaleWidth - Dimension*Len(StrDisplay)) / 2
        x0 = x + (fieldWidth - cellPitch * static_cast<int>(text.size())) / 2;
    }

    for (int i = 0; i < static_cast<int>(text.size()); ++i) {
        const char raw = text[i];
        const char c = static_cast<char>(std::toupper(static_cast<unsigned char>(raw)));

        if (c == '@') {
            BlitCell(dest, x0 + cellPitch * i, y, {27, 0}, scale);
            BlitCell(dest, x0 + cellPitch * (i + 1), y, {28, 0}, scale);
        } else if (c == ' ') {
            BlitCell(dest, x0 + cellPitch * (i + 1), y, {29, 0}, scale);
        } else {
            BlitCell(dest, x0 + cellPitch * i, y, CharToCell(c), scale);
        }
    }
}

} // namespace xmad::gfx
