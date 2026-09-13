#pragma once

#include <cstdint>

// Clean-room reproduction of the original's channel-level LED gradient -
// pixel-exact, extracted directly from Source/xmP.RES's PICGPH resource (a
// 3x26 vertical strip). xmp.frm's SpectrumSin/SpectrumDes level meters
// (InternalSpectrum.ctl's WriteSpec/Modello) work by revealing more of this
// fixed, never-redrawn bitmap from the bottom up as the level rises - so
// what actually changes color with level is just which fixed row of this
// gradient becomes the topmost visible one. Our meter renders as discrete
// LED segments rather than replicating that reveal-a-static-bitmap
// mechanic pixel-for-pixel, so this exposes the same gradient as a pure
// function of position instead.

namespace xmad::gfx {

struct RGB {
    uint8_t r, g, b;
};

// position: 0.0 = bottom of the meter (pure green, RGB 0/230/0), 1.0 = top
// (pure yellow, RGB 230/230/0), with the real gradient's asymmetric
// transition in between (green holds for the bottom ~15%, yellow for the
// top ~15%, gradient through the middle ~70%) - not a symmetric or
// artificially-rounded curve, since this is the original's actual
// extracted pixel data, not a re-derived approximation.
RGB LevelGradientColor(float position);

} // namespace xmad::gfx
