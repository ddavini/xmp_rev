#pragma once

#include <string>

#include "gfx/image.h"

namespace xmad::app {

// Every bitmap the main player window needs, loaded once at startup from
// assets/skin/ - most are copies of the originals in Source/Img_new/,
// renamed for clarity; modeNone/Mono/Stereo/XSound are instead extracted
// directly from Source/xmP.RES's compiled MODO0/MONOON/STEREOON/XSOUNDON
// resources (a standard Win32 .RES file - RT_BITMAP entries are a
// BITMAPINFOHEADER + palette + pixels, missing only the 14-byte
// BITMAPFILEHEADER a loose .bmp needs, which is trivial to synthesize),
// since those specific icons were never loose files anywhere in the tree.
struct Skin {
    gfx::Image btnBack, btnPlay, btnStop, btnNext, btnPause, btnEject;
    gfx::Image btnInfo, btnVol, btnMute, btnSpecMode;
    // "shock" is xmp.frm's LowHigh control (ToolTipText "Shock", despite
    // hosting a Japanese-character logo bitmap - LOGOJAP). Originally
    // ported here as a hand-drawn guess at what that tiny 16x16 logo
    // might look like; the user (the original app's author) later called
    // that guess out as illegibly wrong and asked for it to actually be
    // the katakana character ダ - now a real font-rendered (Hiragino Sans,
    // downsampled with antialiasing preserved rather than a hard
    // black/white threshold, since a clean binary threshold lost the
    // dakuten voicing marks at 16px) small bitmap, not a redrawn
    // approximation. LowHigh_Click in the original just retriggers a
    // random title-scroll effect - unrelated to a later TODO wanting this
    // same icon to open an About page, which isn't wired up yet.
    gfx::Image logo, lite, shock, minimize;
    // About window content (TODO: "create about page linked to the
    // japanese character click"). aboutIcon is the real extracted
    // skull-and-crossbones .ico (SKULL resource from xmP.RES, the
    // original's own compiled app icon - also reused as the macOS Dock
    // icon, see assets/icon/) at its native 32x32; aboutZLogo is the
    // user-supplied Zolnetwork brand mark, downscaled smoothly (it's a
    // photographic-style logo, not pixel art, so LANCZOS rather than
    // nearest-neighbor). Neither the BMP loader nor the draw path support
    // alpha, so both are pre-flattened onto the app's own window-fill
    // color (0x05,0x06,0x03) at asset-generation time rather than relying
    // on any runtime transparency.
    gfx::Image aboutIcon, aboutZLogo;
    // TODO: "there are visualizations missing from the original" - the
    // real xmp.frm has THREE panels sharing the analyzer's box, cycled by
    // clicking it (analyzer -> idle logo -> CardioOSC -> analyzer):  the
    // 6-submode spectrum/oscilloscope analyzer (already ported), a static
    // "CPULESS" idle branding image (ImgLogo, loaded from the real
    // LOGOXMPCPRS resource - a genuine embedded JPEG, not a bitmap like
    // every other asset here), and CardioOSC (a stereo VU+scrolling-
    // oscilloscope display - see main.cpp's CardioOSC drawing code, no
    // extra asset needed there since it reuses the same PICGPH gradient
    // gfx::LevelGradientColor already ports).
    gfx::Image idleLogo;
    gfx::Image volUp, volDown; // xmSlide.ctl's FRECCIAUP/FRECCIADWN
    gfx::Image plClear, plDelete, plSave, plSeek; // Listone.frm's PICCLEAR/PICDELETE/PICSAVE/PICSEEK (Up/Down reuse volUp/volDown - same FRECCIAUP/FRECCIADWN resource)
    gfx::Image modeNone, modeMono, modeStereo, modeXSound; // picModo's MODO0/MONOON/STEREOON/XSOUNDON
    gfx::Image displayFont;

    static Skin Load(const std::string& dir);
};

} // namespace xmad::app
