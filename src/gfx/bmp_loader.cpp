#include "gfx/image.h"

#include <cstdint>
#include <cstring>
#include <fstream>
#include <stdexcept>

namespace xmad::gfx {

namespace {

uint16_t ReadU16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
uint32_t ReadU32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}
int32_t ReadI32(const uint8_t* p) { return static_cast<int32_t>(ReadU32(p)); }

struct Bgra {
    uint8_t b, g, r, a;
};

// Decodes an 8bpp RLE8-compressed BMP scanline stream into a flat,
// bottom-up index buffer of size width*height.
void DecodeRLE8(const uint8_t* data, size_t dataSize, int width, int height,
                 std::vector<uint8_t>& indices) {
    indices.assign(static_cast<size_t>(width) * height, 0);
    size_t pos = 0;
    int x = 0, y = 0;

    auto putPixel = [&](int px, int py, uint8_t idx) {
        if (px >= 0 && px < width && py >= 0 && py < height) {
            indices[static_cast<size_t>(py) * width + px] = idx;
        }
    };

    while (pos + 1 < dataSize) {
        const uint8_t count = data[pos++];
        const uint8_t value = data[pos++];

        if (count > 0) {
            // Encoded run: `count` pixels of `value`.
            for (uint8_t i = 0; i < count; ++i) putPixel(x + i, y, value);
            x += count;
        } else {
            // Escape codes.
            if (value == 0) {
                // End of line.
                x = 0;
                ++y;
            } else if (value == 1) {
                // End of bitmap.
                break;
            } else if (value == 2) {
                // Delta: next two bytes are dx, dy.
                if (pos + 1 >= dataSize) break;
                x += data[pos++];
                y += data[pos++];
            } else {
                // Absolute mode: `value` literal indices follow, padded to
                // an even byte count.
                const uint8_t literalCount = value;
                for (uint8_t i = 0; i < literalCount && pos < dataSize; ++i) {
                    putPixel(x + i, y, data[pos++]);
                }
                x += literalCount;
                if (literalCount & 1) ++pos; // skip pad byte
            }
        }
    }
}

} // namespace

Image LoadBMP(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("LoadBMP: cannot open " + path);

    std::vector<uint8_t> buf((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (buf.size() < 54 || buf[0] != 'B' || buf[1] != 'M') {
        throw std::runtime_error("LoadBMP: not a BMP file: " + path);
    }

    const uint32_t dataOffset = ReadU32(&buf[10]);
    const uint32_t infoHeaderSize = ReadU32(&buf[14]);
    if (infoHeaderSize < 40) throw std::runtime_error("LoadBMP: unsupported header in " + path);

    const int32_t rawWidth = ReadI32(&buf[18]);
    const int32_t rawHeight = ReadI32(&buf[22]);
    const uint16_t bitCount = ReadU16(&buf[28]);
    const uint32_t compression = ReadU32(&buf[30]);

    const int width = rawWidth;
    const bool topDown = rawHeight < 0;
    const int height = topDown ? -rawHeight : rawHeight;

    if (bitCount != 8 && bitCount != 24) {
        throw std::runtime_error("LoadBMP: unsupported bit depth (" + std::to_string(bitCount) +
                                  ") in " + path);
    }

    Image img;
    img.width = width;
    img.height = height;
    img.rgba.resize(static_cast<size_t>(width) * height * 4);

    if (bitCount == 24) {
        if (compression != 0) throw std::runtime_error("LoadBMP: compressed 24bpp not supported: " + path);
        const size_t rowSize = ((static_cast<size_t>(width) * 3 + 3) / 4) * 4;
        for (int y = 0; y < height; ++y) {
            const int srcRow = topDown ? y : (height - 1 - y);
            const uint8_t* row = &buf[dataOffset + srcRow * rowSize];
            for (int x = 0; x < width; ++x) {
                uint8_t* out = img.pixel(x, y);
                out[0] = row[x * 3 + 2]; // R
                out[1] = row[x * 3 + 1]; // G
                out[2] = row[x * 3 + 0]; // B
                out[3] = 255;
            }
        }
        return img;
    }

    // 8bpp paletted.
    const uint32_t paletteOffset = 14 + infoHeaderSize;
    const uint32_t clrUsed = ReadU32(&buf[46]);
    const int numColors = clrUsed != 0 ? static_cast<int>(clrUsed) : 256;

    std::vector<Bgra> palette(256, Bgra{0, 0, 0, 255});
    for (int i = 0; i < numColors && paletteOffset + static_cast<uint32_t>(i) * 4 + 4 <= buf.size(); ++i) {
        const uint8_t* p = &buf[paletteOffset + i * 4];
        palette[i] = {p[0], p[1], p[2], 255};
    }

    std::vector<uint8_t> indices;
    if (compression == 1) { // BI_RLE8
        DecodeRLE8(&buf[dataOffset], buf.size() - dataOffset, width, height, indices);
        // DecodeRLE8 fills bottom-up rows into a row-major (y=0 is bottom) buffer.
        for (int y = 0; y < height; ++y) {
            const int srcRow = height - 1 - y; // flip to top-down output
            for (int x = 0; x < width; ++x) {
                const uint8_t idx = indices[static_cast<size_t>(srcRow) * width + x];
                const Bgra& c = palette[idx];
                uint8_t* out = img.pixel(x, y);
                out[0] = c.r;
                out[1] = c.g;
                out[2] = c.b;
                out[3] = 255;
            }
        }
    } else if (compression == 0) { // BI_RGB, uncompressed
        const size_t rowSize = ((static_cast<size_t>(width) + 3) / 4) * 4;
        for (int y = 0; y < height; ++y) {
            const int srcRow = topDown ? y : (height - 1 - y);
            const uint8_t* row = &buf[dataOffset + srcRow * rowSize];
            for (int x = 0; x < width; ++x) {
                const Bgra& c = palette[row[x]];
                uint8_t* out = img.pixel(x, y);
                out[0] = c.r;
                out[1] = c.g;
                out[2] = c.b;
                out[3] = 255;
            }
        }
    } else {
        throw std::runtime_error("LoadBMP: unsupported compression type in " + path);
    }

    return img;
}

} // namespace xmad::gfx
