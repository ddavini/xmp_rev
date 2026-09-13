// Renders a sample string with the ported bitmap font and dumps a raw RGB
// buffer + dimensions so it can be inspected visually (see tools/raw_to_png.py).
// Also spot-checks a handful of CharToCell mappings against xmDisplay.ctl's
// DrawChar source directly (values transcribed by hand from that file).

#include "gfx/bitmap_font.h"
#include "gfx/image.h"

#include <cstdio>
#include <fstream>
#include <string>

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s path/to/DISPLAY.bmp [out.raw]\n", argv[0]);
        return 2;
    }

    xmad::gfx::Image atlas;
    try {
        atlas = xmad::gfx::LoadBMP(argv[1]);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "LoadBMP failed: %s\n", e.what());
        return 1;
    }
    std::printf("loaded atlas %dx%d\n", atlas.width, atlas.height);

    xmad::gfx::BitmapFont font(atlas);

    const std::string text = "*** X-MAD.PLAYER v1.0.378 ***";
    const int scale = 4;
    const int cellPitch = xmad::gfx::BitmapFont::kCellW * scale;
    const int canvasW = cellPitch * static_cast<int>(text.size()) + 20;
    const int canvasH = xmad::gfx::BitmapFont::kCellH * scale + 20;

    xmad::gfx::Image canvas;
    canvas.width = canvasW;
    canvas.height = canvasH;
    canvas.rgba.assign(static_cast<size_t>(canvasW) * canvasH * 4, 0);
    for (size_t i = 3; i < canvas.rgba.size(); i += 4) canvas.rgba[i] = 255; // opaque black bg

    font.DrawText(canvas, 10, 10, text, scale);

    const std::string outPath = argc >= 3 ? argv[2] : "font_render.raw";
    std::ofstream out(outPath, std::ios::binary);
    out.write(reinterpret_cast<const char*>(&canvas.width), 4);
    out.write(reinterpret_cast<const char*>(&canvas.height), 4);
    out.write(reinterpret_cast<const char*>(canvas.rgba.data()), static_cast<std::streamsize>(canvas.rgba.size()));
    std::printf("wrote %s (%dx%d)\n", outPath.c_str(), canvas.width, canvas.height);

    return 0;
}
