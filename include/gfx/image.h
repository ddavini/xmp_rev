#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Minimal, dependency-free image type + BMP decoder. The original skin
// assets (Source/Img_new/*.bmp / *.BMP) are only ever uncompressed 24bpp or
// RLE8-compressed 8bpp paletted Windows BMPs, so that's all this supports -
// no need to vendor a general-purpose image library for two BMP variants.

namespace xmad::gfx {

struct Image {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> rgba; // width*height*4, row-major, top-down

    uint8_t* pixel(int x, int y) { return &rgba[(static_cast<size_t>(y) * width + x) * 4]; }
    const uint8_t* pixel(int x, int y) const { return &rgba[(static_cast<size_t>(y) * width + x) * 4]; }
};

// Throws std::runtime_error on any format it doesn't recognize.
Image LoadBMP(const std::string& path);

} // namespace xmad::gfx
