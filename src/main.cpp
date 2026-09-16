#include <SDL2/SDL.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#include <mach-o/dyld.h>
#include <climits>
#elif defined(__linux__)
#include <climits>
#include <unistd.h>
#endif

#include "app/file_dialog.h"
#include "app/layout.h"
#ifdef __APPLE__
#include "app/main_menu.h"
#include "app/media_remote.h"
#include "app/menu_bar_icon.h"
#endif
#include "app/playlist.h"
#include "app/session.h"
#include "app/version.h"
#include "app/window_snap.h"
#include "app/skin.h"
#include "audio/engine.h"
#include "audio/tags.h"
#include "dsp/fft.h"
#include "gfx/bitmap_font.h"
#include "gfx/image.h"
#include "gfx/level_meter.h"

namespace {

using namespace xmad;

// Uploads an app::gfx::Image (RGBA8, R,G,B,A byte order) as a static SDL
// texture. SDL_PIXELFORMAT_RGBA32 is SDL's endian-adjusting alias that
// always means "bytes in memory are R,G,B,A", matching our Image layout
// regardless of host endianness.
SDL_Texture* UploadTexture(SDL_Renderer* renderer, const gfx::Image& img) {
    SDL_Texture* tex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC,
                                          img.width, img.height);
    if (!tex) return nullptr;
    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    SDL_UpdateTexture(tex, nullptr, img.rgba.data(), img.width * 4);
    return tex;
}

// SDL_WINDOW_ALLOW_HIGHDPI windows report their *size* in points (what we
// pass to SDL_CreateWindow, and what mouse-event coordinates use), but the
// renderer's actual backing store on a Retina display is bigger (2x, on
// current Macs) - SDL_GetRendererOutputSize returns that real pixel size.
// Without querying it and adjusting SDL_RenderSetScale to match, our draws
// still target a 1x-resolution buffer that the OS then has to blur-upscale
// to fill the Retina backing store - exactly the reported blurriness. Note
// this only touches the *render* scale; window creation size and mouse
// coordinates stay in points; per app::WinRect docs those are deliberately
// different units.
void ApplyHiDpiRenderScale(SDL_Renderer* renderer, int logicalW, int logicalH) {
    int outW = logicalW, outH = logicalH;
    SDL_GetRendererOutputSize(renderer, &outW, &outH);
    // Must be the *exact* outW/outH ratio, not rounded: SDL_RenderSetScale
    // only changes how logical coordinates map onto the renderer's already-
    // allocated real backing store - it can never make that backing store
    // bigger. A previous version of this rounded to the nearest whole pixel
    // multiple to fix uneven nearest-neighbor glyph scaling (see git log),
    // but on a fractional-scale desktop compositor (e.g. a 5/3 = 1.667x
    // display scale, which combines with our own UI-scale percent into a
    // non-integer ratio at most zoom levels) rounding UP made draws target
    // pixels past the real backing store's edge - the actual cause of
    // windows rendering with content clipped/pushed outside their visible
    // bounds after a rescale. Exact-fit is the only value that's always
    // safe; text staying slightly uneven at non-integer ratios is a
    // cosmetic tradeoff, not a correctness one.
    SDL_RenderSetScale(renderer, static_cast<float>(outW) / logicalW, static_cast<float>(outH) / logicalH);
}

// TODO: "Add a setting for UI scale / text+button size". The full set of
// UI-scale levels the View menu / Zoom In/Out step through, and that
// SnapUiScalePercent below snaps any stored value - including a corrupt/
// future one read back from settings.cfg - to, rather than trusting the
// raw number all the way into window-size arithmetic. Shared by
// SnapUiScalePercent, the Linux View menu's Zoom In/Out (main.cpp's
// processEvent), and macOS's Cmd+/Cmd- menu equivalents (main_menu.h's
// UiScaleMenuAction::ZoomIn/ZoomOut) so all three stay in sync.
constexpr int kUiScaleLevels[] = {100, 125, 150, 200, 250, 300, 350, 400};

int SnapUiScalePercent(int raw) {
    int best = kUiScaleLevels[0];
    for (int lvl : kUiScaleLevels) {
        if (std::abs(lvl - raw) < std::abs(best - raw)) best = lvl;
    }
    return best;
}

int ScaledDim(int logical, double scale) { return static_cast<int>(std::lround(logical * scale)); }

// TODO: "Add a setting for UI scale / text+button size" - the shared
// stacking geometry for all 5 windows at a given scale/anchor. Computed
// once at startup and again on every live rescale (see applyUiScale in
// main()), so there's exactly one place this stacking math lives.
struct WindowLayout {
    SDL_Rect main, eq, playlist, info, about;
};
WindowLayout ComputeWindowLayout(double scale, int mainX, int mainY) {
    WindowLayout L;
    L.main = {mainX, mainY, ScaledDim(app::layout::kWindowW, scale), ScaledDim(app::layout::kWindowH, scale)};
    L.eq = {mainX, mainY + L.main.h, ScaledDim(app::layout::kEqWindowW, scale),
            ScaledDim(app::layout::kEqWindowH, scale)};
    L.playlist = {mainX, mainY + L.main.h + L.eq.h, ScaledDim(app::layout::kPlaylistWindowW, scale),
                  ScaledDim(app::layout::kPlaylistWindowH, scale)};
    L.info = {mainX + L.main.w, mainY, ScaledDim(app::layout::kInfoWindowW, scale),
              ScaledDim(app::layout::kInfoWindowH, scale)};
    L.about = {mainX + L.main.w, mainY + L.info.h, ScaledDim(app::layout::kAboutWindowW, scale),
               ScaledDim(app::layout::kAboutWindowH, scale)};
    return L;
}

void DrawTextureAt(SDL_Renderer* renderer, SDL_Texture* tex, int x, int y) {
    int w, h;
    SDL_QueryTexture(tex, nullptr, nullptr, &w, &h);
    SDL_Rect dst{x, y, w, h};
    SDL_RenderCopy(renderer, tex, nullptr, &dst);
}

// Stretches tex to fill exactly the given box - matches ImgLogo.Stretch =
// True in the original (every other bitmap this app draws is blitted at
// its own native size via DrawTextureAt above; this one control was the
// only one in xmp.frm actually set to stretch its picture).
void DrawTextureFit(SDL_Renderer* renderer, SDL_Texture* tex, int x, int y, int w, int h) {
    SDL_Rect dst{x, y, w, h};
    SDL_RenderCopy(renderer, tex, nullptr, &dst);
}

// Builds a one-off text canvas with the bitmap font and uploads it as a
// texture. Fine for now (title/status text is static); once the decode
// engine is wired up this'll be rebuilt only when the string changes.
SDL_Texture* RenderTextTexture(SDL_Renderer* renderer, const gfx::BitmapFont& font,
                                std::string_view text, int fieldWidth) {
    gfx::Image canvas;
    canvas.width = fieldWidth;
    canvas.height = gfx::BitmapFont::kCellH;
    // Fully transparent, not opaque black: BitmapFont::BlitCell now
    // treats the atlas's black "off" pixels as a transparent color key
    // (see its comment), so nothing ever actually paints these leftover
    // alpha=0 pixels - keeping the canvas transparent here too is what
    // lets this texture be drawn over a colored fill (see the PL/EQ
    // toggle buttons) without punching a black rectangle through it.
    canvas.rgba.assign(static_cast<size_t>(canvas.width) * canvas.height * 4, 0);
    font.DrawText(canvas, 0, 0, text, /*scale=*/1, fieldWidth);
    return UploadTexture(renderer, canvas);
}

// RenderTextTexture (rasterize + upload) redone every single frame for text
// that usually hasn't changed since the last frame - duration/freq/volume
// readouts tick at most once a second, EQ band/preset labels and the
// playlist's static header never change at all - was real, measurable idle
// CPU (found via a live `sample` profile: with the render loop's frame-rate
// cap already in place, most of the remaining per-frame cost was still text
// texture churn). One of these per on-screen text field, kept alive for the
// field's lifetime rather than recreated each call.
struct CachedTextTexture {
    SDL_Texture* tex = nullptr;
    std::string lastText;
    int lastFieldWidth = -1;

    SDL_Texture* Get(SDL_Renderer* renderer, const gfx::BitmapFont& font, std::string_view text, int fieldWidth) {
        if (!tex || lastFieldWidth != fieldWidth || lastText != text) {
            if (tex) SDL_DestroyTexture(tex);
            tex = RenderTextTexture(renderer, font, text, fieldWidth);
            lastText.assign(text);
            lastFieldWidth = fieldWidth;
        }
        return tex;
    }
};

// Snapshot of everything a secondary window's frame actually depends on,
// compared against the previous frame's snapshot in the main loop. Even
// after RenderTextTexture calls are cached (see above), SDL_RenderPresent
// itself has a fixed per-call cost on this platform - it re-uploads the
// entire window surface to a Metal-backed texture regardless of whether any
// pixel actually changed (confirmed via a live `sample` profile) - so the
// only way to avoid paying it every frame is to skip the draw+present pair
// outright when nothing changed. Playlist/EQ/Info/About have no continuous
// animation of their own (unlike Main's spectrum/VU meters), so while idle
// almost every frame is an exact repeat of the last.
struct PlaylistFrameKey {
    uint64_t playlistGen = 0;
    int currentIndex = -2;
    int selected = -2;
    int scrollOffset = -1;
    int pressedButton = -2;
    unsigned sampleRate = 0;
    unsigned channels = 0;
    bool saveMenuOpen = false;
    bool clearConfirmOpen = false;
    bool operator==(const PlaylistFrameKey&) const = default;
};

struct EqFrameKey {
    std::array<int, audio::Equalizer::kBands> bands{};
    int pressedSlider = -2;
    int currentPreset = -2;
    bool perSongEq = false;
    bool operator==(const EqFrameKey&) const = default;
};

struct InfoFrameKey {
    uint64_t playlistGen = 0;
    int currentIndex = -2;
    unsigned channels = 0;
    unsigned sampleRate = 0;
    double durationSeconds = -1.0;
    bool operator==(const InfoFrameKey&) const = default;
};

// Renders a bar whose bottom `litFraction` is filled with a green->amber
// vertical gradient (green at the base, amber at the tip) and the rest left
// dark. Stands in for the original's gph/gpeak bar-stamp bitmaps: those are
// compiled VB resources (PICGPH/PICPEAKS) with no loose-file fallback
// anywhere in the source, so unlike the button icons their exact source art
// isn't recoverable - this reproduces BitBltSpec's *behavior* (a
// green-to-amber level bar) rather than guessing at a specific bitmap.
void DrawGradientBar(SDL_Renderer* r, int x, int y, int w, int h, float litFraction) {
    const int litPx = static_cast<int>(h * std::clamp(litFraction, 0.0f, 1.0f));
    for (int row = 0; row < litPx; ++row) {
        const float t = static_cast<float>(row) / std::max(1, h - 1); // 0 at base, 1 at tip
        const uint8_t red = static_cast<uint8_t>(0x3d + t * (0xd8 - 0x3d));
        const uint8_t green = static_cast<uint8_t>(0xff - t * (0xff - 0xe3));
        const uint8_t blue = static_cast<uint8_t>(0x74 - t * 0x74);
        SDL_SetRenderDrawColor(r, red, green, blue, 255);
        SDL_RenderDrawLine(r, x, y + h - 1 - row, x + w - 1, y + h - 1 - row);
    }
}

// Draws a 1px marker line across the column at the given height - the
// bar-modes' peak-hold dot (blue, dashed in a real screenshot of the
// original - not the green solid line an earlier pass here guessed at),
// or Fade FFT's trailing peak line (dashed off by default: no reference
// screenshot of that mode exists to confirm/deny it).
void DrawPeakMarker(SDL_Renderer* r, int x, int y, int w, int h, float heightPx, uint8_t red, uint8_t green,
                     uint8_t blue, bool dashed = false) {
    const int row = std::clamp(static_cast<int>(heightPx), 0, h - 1);
    const int py = y + h - 1 - row;
    SDL_SetRenderDrawColor(r, red, green, blue, 255);
    if (dashed) {
        for (int px = x; px < x + w; px += 2) SDL_RenderDrawPoint(r, px, py);
    } else {
        SDL_RenderDrawLine(r, x, py, x + w - 1, py);
    }
}

// DisgnaxmMP3FFT's "Fade FFT" look: a solid vertical-gradient fill (green at
// the base, dark red at the top - matches the original's RGB(64,GG,0) with
// GG derived from absolute row position, a loud-peak-warning convention)
// rather than DrawGradientBar's green-to-amber gradient used elsewhere.
void DrawFadeColumn(SDL_Renderer* r, int x, int y, int w, int h, float fillPx) {
    const int litPx = std::clamp(static_cast<int>(fillPx), 0, h);
    for (int row = 0; row < litPx; ++row) {
        const float t = static_cast<float>(row) / std::max(1, h - 1); // 0 at base, 1 at tip
        const uint8_t green = static_cast<uint8_t>(std::clamp((1.0f - t) * 255.0f, 0.0f, 255.0f));
        SDL_SetRenderDrawColor(r, 64, green, 0, 255);
        SDL_RenderDrawLine(r, x, y + h - 1 - row, x + w - 1, y + h - 1 - row);
    }
}

// Matches xmp.frm's CommandImg_MouseDown Select Case (Index 0-5) exactly:
// Back/Next are playlist navigation (MoveInMp3), Eject opens a file dialog
// (OpenMp3dlg) - neither playlist nor file dialog exist yet, so those three
// are documented no-ops for now rather than fabricated behavior.
enum class TransportAction { Back, Play, Stop, Next, Pause, Eject };

std::string BaseName(const std::string& path) {
    const auto slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

// Average bitrate, in Kbit/s: file size / duration - exact for CBR, an
// honest approximation for VBR (dr_mp3 doesn't expose a genuine running
// bitrate the way the original's xmMP3_getPlayBitRate did, per the
// TODO this replaces - "the Khz and bit rate labels are fake"). Returns
// -1 if it can't be computed (file unreadable, or duration not yet
// known), which callers show as a placeholder rather than a bogus 0.
int ComputeAvgBitrateKbps(const std::string& path, double durationSeconds) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f || durationSeconds <= 0.0) return -1;
    const double bytes = static_cast<double>(f.tellg());
    return static_cast<int>(std::lround((bytes * 8.0 / durationSeconds) / 1000.0));
}

struct Bevel {
    static void Draw(SDL_Renderer* r, int x, int y, int w, int h) {
        SDL_SetRenderDrawColor(r, 0x05, 0x06, 0x03, 255);
        SDL_Rect fill{x, y, w, h};
        SDL_RenderFillRect(r, &fill);
        SDL_SetRenderDrawColor(r, 0xad, 0xb0, 0xa3, 255);
        SDL_Rect outer{x, y, w, h};
        SDL_RenderDrawRect(r, &outer);
        SDL_SetRenderDrawColor(r, 0x23, 0x26, 0x20, 255);
        SDL_Rect inner{x + 1, y + 1, w - 2, h - 2};
        SDL_RenderDrawRect(r, &inner);
    }
};

// A sunken (inset, not raised) bevelled box - matches the bordered panel
// the readout cluster (Time/Mode/BitRate/Freq) sits in, visible in a real
// screenshot of the original: a distinct recessed panel, not loose text.
void DrawInsetPanel(SDL_Renderer* r, int x, int y, int w, int h) {
    SDL_SetRenderDrawColor(r, 0x03, 0x04, 0x02, 255);
    SDL_Rect fill{x, y, w, h};
    SDL_RenderFillRect(r, &fill);
    SDL_SetRenderDrawColor(r, 0x1a, 0x1d, 0x14, 255); // dark top/left = sunken
    SDL_RenderDrawLine(r, x, y, x + w - 1, y);
    SDL_RenderDrawLine(r, x, y, x, y + h - 1);
    SDL_SetRenderDrawColor(r, 0x3a, 0x40, 0x30, 255); // lighter bottom/right
    SDL_RenderDrawLine(r, x, y + h - 1, x + w - 1, y + h - 1);
    SDL_RenderDrawLine(r, x + w - 1, y, x + w - 1, y + h - 1);
}

void DrawDashedVLine(SDL_Renderer* r, int x, int y0, int y1, uint8_t red, uint8_t green, uint8_t blue) {
    SDL_SetRenderDrawColor(r, red, green, blue, 255);
    for (int y = y0; y < y1; y += 3) SDL_RenderDrawLine(r, x, y, x, std::min(y1 - 1, y + 1));
}

void DrawDashedHLine(SDL_Renderer* r, int x0, int x1, int y, uint8_t red, uint8_t green, uint8_t blue) {
    SDL_SetRenderDrawColor(r, red, green, blue, 255);
    for (int x = x0; x < x1; x += 3) SDL_RenderDrawLine(r, x, y, std::min(x1 - 1, x + 1), y);
}

// No loose-file asset exists for the original's "EXIT" resource (a
// compiled-only icon, like PICGPH/PICPEAKS - see the spectrum-bar note
// elsewhere), so MenuBarPic's close glyph is drawn procedurally: a small
// green X, in the same spot the original's MenuBarPic occupied.
void DrawCloseIcon(SDL_Renderer* r, int x, int y, int size) {
    SDL_SetRenderDrawColor(r, 0x3d, 0xff, 0x74, 255);
    SDL_RenderDrawLine(r, x, y, x + size - 1, y + size - 1);
    SDL_RenderDrawLine(r, x, y + size - 1, x + size - 1, y);
}

// A real header strip for the playlist/EQ windows (borderless, so there's
// no OS title bar) - not in the original (Listone.frm/frmEQ.frm drew their
// MenuBarPic directly on the bevelled background with no distinct header
// band), added so the drag zone has a visually obvious home and doesn't
// read as "clicking blank space happens to move the window."
void DrawWindowHeader(SDL_Renderer* r, int windowW, int stripH) {
    SDL_SetRenderDrawColor(r, 0x16, 0x19, 0x11, 255);
    SDL_Rect bar{1, 1, windowW - 2, stripH - 1};
    SDL_RenderFillRect(r, &bar);
    SDL_SetRenderDrawColor(r, 0x23, 0x26, 0x20, 255);
    SDL_RenderDrawLine(r, 1, stripH, windowW - 2, stripH);
}

// Directory the running binary actually lives in - NOT the current working
// directory, which is whatever launched us (unset/home dir for a Dock or
// Finder double-click, no relation to the repo at all). Needed so the app
// can find its own assets without being told where they are on the command
// line; see ResolveDefaultAssetDir below.
std::string ExecutableDir() {
#ifdef __APPLE__
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size); // first call: just reports the needed size
    std::vector<char> buf(size);
    if (_NSGetExecutablePath(buf.data(), &size) != 0) return "";
    char resolved[PATH_MAX];
    if (!realpath(buf.data(), resolved)) return "";
    std::string path(resolved);
#elif defined(__linux__)
    char buf[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len <= 0) return "";
    std::string path(buf, static_cast<size_t>(len));
#else
    return "";
#endif
    const auto pos = path.find_last_of('/');
    return pos == std::string::npos ? "" : path.substr(0, pos);
}

bool LooksLikeSkinDir(const std::string& dir) {
    // Cheap existence probe: display_font.bmp is one of the assets every
    // skin directory must have (BitmapFont construction below hard-requires
    // it), so its presence is a reliable "is this really a skin dir" check.
    std::ifstream f(dir + "/display_font.bmp");
    return f.good();
}

// No assetDir given on the command line: figure out where our own assets
// are instead of assuming the caller's shell happens to be sitting in the
// repo root. Tries, in order: the macOS .app bundle's bundled copy
// (Contents/MacOS/xmad -> Contents/Resources/skin), the flat "skin" dir
// next to the binary (the extracted Linux tarball layout), an "assets/skin"
// dir next to the binary (e.g. a hand-copied build), then finally falls
// back to the plain "assets/skin" relative path the dev workflow
// (`make run` / `./build/xmad` from the repo root) has always used - so
// nothing already working regresses.
std::string ResolveDefaultAssetDir() {
    const std::string exeDir = ExecutableDir();
    if (!exeDir.empty()) {
        const std::string bundleSkin = exeDir + "/../Resources/skin";
        if (LooksLikeSkinDir(bundleSkin)) return bundleSkin;
        // Flat layout the Linux tarball actually ships (see Makefile's
        // `tarball` target: xmad + skin/ side by side, no "assets/" prefix).
        const std::string flatSkin = exeDir + "/skin";
        if (LooksLikeSkinDir(flatSkin)) return flatSkin;
        const std::string sideSkin = exeDir + "/assets/skin";
        if (LooksLikeSkinDir(sideSkin)) return sideSkin;
    }
    return "assets/skin";
}

} // namespace

int main(int argc, char** argv) {
    std::string assetDir;
    std::string dumpFramePath;         // if set: render exactly one main-window frame, save it, exit
    std::string dumpPlaylistFramePath; // same, for the playlist window
    std::string playPath;              // shorthand for a one-entry playlist
    std::string clickName;             // debug: synthesize this transport button press first
    int utilClickCount = 0;            // debug: synthesize N SpecMode-cycle clicks first
    int visTicks = 0;                  // debug: render this many silent frames before the final dump
    int simClickSpecModeCount = 0;     // debug: push N real SDL clicks on SpecMode through the real event path
    std::vector<std::array<std::string, 3>> simClicks; // debug: [window(main/pl/eq), x, y] real clicks, in order
    int hoverX = -1, hoverY = -1; // debug: synthesize a real mouse-motion to this main-window position first
    bool visPauseFirst = false;        // debug: engine.Pause() before the tick loop (to watch decay, not attack)
    std::string playlistClickName;     // debug: synthesize this playlist button press first
    std::string playlistFile;          // M3U file to load into the playlist
    int selectRowArg = -1;             // debug: synthesize selecting this playlist row first
    bool playlistQuickSaveTest = false; // debug: invoke Quick Save's write lambda directly
    std::string playlistSaveAsTestPath; // debug: invoke Save As's write lambda with this path, bypassing the OS dialog
    std::string dropPath;              // debug: synthesize dropping this file first
    std::string dumpEqFramePath;       // same dump mechanism, for the EQ window
    std::string dumpInfoFramePath;     // same dump mechanism, for the Info window
    std::string dumpAboutFramePath;    // same dump mechanism, for the About window
    std::string eqPresetArg;           // debug: synthesize clicking this preset first
    int eqSetBandArg = -1, eqSetValueArg = 0; // debug: synthesize dragging this band to this value
    int setVolumeArg = -1; // debug: synthesize dragging the volume slider to this 0..100 value
    // debug: synthesize minimize/restore cascades and check the other windows followed.
    // Always pair with --auto-advance-test <N> (or another bounded-exit flag) - on its
    // own this falls through into the normal unbounded interactive loop and spins at
    // full CPU forever with nothing to send it SDL_QUIT (hit this firsthand: a bare
    // --test-minimize-cascade run pegged a core for two minutes before being killed).
    bool testMinimizeCascade = false;
    int toggleXSoundCount = 0; // debug: synthesize pressing 'x' this many times first
    double autoAdvanceTestSeconds = 0.0; // debug: run the real main loop for N seconds, log currentIndex changes
    std::string clearReloadTestPath;     // debug: Clear -> add this file -> Play, then report engine state
    std::vector<std::string> positionalTracks; // bare args after assetDir = playlist entries
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--version" || a == "-v") {
            std::cout << "X.MaD Player Revival v" << app::kVersion << "\n";
            return 0;
        } else if (a == "--dump-frame" && i + 1 < argc) {
            dumpFramePath = argv[++i];
        } else if (a == "--drop" && i + 1 < argc) {
            dropPath = argv[++i];
        } else if (a == "--dump-playlist-frame" && i + 1 < argc) {
            dumpPlaylistFramePath = argv[++i];
        } else if (a == "--dump-eq-frame" && i + 1 < argc) {
            dumpEqFramePath = argv[++i];
        } else if (a == "--dump-info-frame" && i + 1 < argc) {
            dumpInfoFramePath = argv[++i];
        } else if (a == "--dump-about-frame" && i + 1 < argc) {
            dumpAboutFramePath = argv[++i];
        } else if (a == "--eq-preset" && i + 1 < argc) {
            eqPresetArg = argv[++i];
        } else if (a == "--eq-band" && i + 2 < argc) {
            eqSetBandArg = std::atoi(argv[++i]);
            eqSetValueArg = std::atoi(argv[++i]);
        } else if (a == "--set-volume" && i + 1 < argc) {
            setVolumeArg = std::atoi(argv[++i]);
        } else if (a == "--toggle-xsound" && i + 1 < argc) {
            toggleXSoundCount = std::atoi(argv[++i]);
        } else if (a == "--test-minimize-cascade") {
            testMinimizeCascade = true;
        } else if (a == "--play" && i + 1 < argc) {
            playPath = argv[++i];
        } else if (a == "--click" && i + 1 < argc) {
            clickName = argv[++i];
        } else if (a == "--specmode-clicks" && i + 1 < argc) {
            utilClickCount = std::atoi(argv[++i]);
        } else if (a == "--vis-ticks" && i + 1 < argc) {
            visTicks = std::atoi(argv[++i]);
        } else if (a == "--vis-pause-first") {
            visPauseFirst = true;
        } else if (a == "--sim-click-specmode" && i + 1 < argc) {
            simClickSpecModeCount = std::atoi(argv[++i]);
        } else if (a == "--sim-click" && i + 3 < argc) {
            std::string win = argv[++i];
            std::string x = argv[++i];
            std::string y = argv[++i];
            simClicks.push_back({win, x, y});
        } else if (a == "--hover" && i + 2 < argc) {
            hoverX = std::atoi(argv[++i]);
            hoverY = std::atoi(argv[++i]);
        } else if (a == "--playlist-click" && i + 1 < argc) {
            playlistClickName = argv[++i];
        } else if (a == "--select-row" && i + 1 < argc) {
            selectRowArg = std::atoi(argv[++i]);
        } else if (a == "--playlist-quick-save") {
            playlistQuickSaveTest = true;
        } else if (a == "--playlist-save-as" && i + 1 < argc) {
            playlistSaveAsTestPath = argv[++i];
        } else if (a == "--playlist" && i + 1 < argc) {
            playlistFile = argv[++i];
        } else if (a == "--auto-advance-test" && i + 1 < argc) {
            autoAdvanceTestSeconds = std::atof(argv[++i]);
        } else if (a == "--clear-reload-test" && i + 1 < argc) {
            clearReloadTestPath = argv[++i];
        } else if (assetDir.empty()) {
            assetDir = a;
        } else {
            positionalTracks.push_back(a);
        }
    }
    if (assetDir.empty()) assetDir = ResolveDefaultAssetDir();

    // On macOS, a click that only focuses a background window is by default
    // swallowed - not delivered as a real SDL_MOUSEBUTTONDOWN - so our
    // window-group-raise logic (see SDL_MOUSEBUTTONDOWN handling below)
    // never runs on that first click. That's exactly the "need two clicks
    // to raise EQ" symptom: whichever of the three windows isn't already
    // key needs an extra click before its click event actually reaches us.
    // This hint makes the very first, focus-granting click count too.
    SDL_SetHint(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "1");

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) {
        std::cerr << "SDL_Init failed: " << SDL_GetError() << "\n";
        return 1;
    }

#ifdef __APPLE__
    // A dedicated custom event code for "menu-bar tray icon was clicked",
    // rather than reusing SDL_WINDOWEVENT_RESTORED/FOCUS_GAINED: an
    // earlier version did that, but hiding Main + switching the app's
    // activation policy (see enterAppTray below) turned out to itself
    // generate a spurious event of that same shape for Main moments
    // later, which was indistinguishable from a real click and caused
    // the app to immediately undo its own minimize.
    const Uint32 kTrayRestoreEventType = SDL_RegisterEvents(1);

    // Same bridging pattern as kTrayRestoreEventType above - pushed by
    // the tray icon's right-click popup's "About" item (see
    // ShowMenuBarIcon), handled in processEvent below by restoring the
    // app and showing the About window specifically.
    const Uint32 kTrayAboutEventType = SDL_RegisterEvents(1);

    // Bluetooth headphones/AirPods/car-stereo play-pause-next-previous
    // presses arrive as MPRemoteCommandCenter callbacks (media_remote.mm),
    // which fire on whatever thread/queue the system chooses - bridged
    // into the SDL event queue the same way as kTrayRestoreEventType
    // above, then handled in processEvent below.
    const Uint32 kMediaRemoteEventType = SDL_RegisterEvents(1);
    app::EnableMediaRemoteCommands(kMediaRemoteEventType);

    // TODO: "Add a setting for UI scale / text+button size" - the View
    // menu's 100/125/150% items and Zoom In/Out/Reset (Cmd+/Cmd-/Cmd+0)
    // push this event, consumed in processEvent below via applyUiScale.
    // Installed onto NSApp's existing mainMenu (see main_menu.h) rather
    // than a from-scratch menu bar.
    const Uint32 kUiScaleEventType = SDL_RegisterEvents(1);
    app::InstallUiScaleMenu(kUiScaleEventType);

    // Effects menu (currently just xSound) - same bridging pattern as
    // the View menu above; consumed in processEvent below via
    // toggleXSound.
    const Uint32 kEffectsEventType = SDL_RegisterEvents(1);
    app::InstallEffectsMenu(kEffectsEventType);
#endif

    // TODO: "Add a setting for UI scale / text+button size" - loaded here,
    // independently of the resumeSession-gated settings load further
    // below, since a display preference should apply on every launch
    // (including an explicit --playlist run), not just when resuming the
    // last session. Forced to 100 for --dump-*-frame so those headless
    // verification runs stay deterministic regardless of a saved
    // preference. Reads settings.cfg a second time (the resumeSession
    // block re-reads it later for its own fields) - a deliberately cheap,
    // additive trade-off rather than restructuring that block's ordering.
    int uiScalePercent = 100;
    {
        app::Settings scalePrefs;
        if (app::LoadSettingsFile(app::SettingsFilePath(), scalePrefs)) {
            uiScalePercent = SnapUiScalePercent(scalePrefs.uiScalePercent);
        }
    }
    const bool dumpingAnyFrameEarly = !dumpFramePath.empty() || !dumpPlaylistFramePath.empty() ||
                                       !dumpEqFramePath.empty() || !dumpInfoFramePath.empty() ||
                                       !dumpAboutFramePath.empty();
    if (dumpingAnyFrameEarly) uiScalePercent = 100;
    double scale = uiScalePercent / 100.0;
#ifdef __APPLE__
    app::SetUiScaleMenuChecked(uiScalePercent); // reflect a resumed non-default scale immediately
#endif
    // Borderless: matches the original's BorderStyle=0 (fully custom-drawn,
    // no OS title bar/chrome). This means there's no native close button or
    // way to drag the window by a title bar anymore - both are implemented
    // by hand below (a drawn MenuBarPic close icon, and click-drag-the-body
    // for movement), same as the original had to.
    const Uint32 kWindowFlags = SDL_WINDOW_SHOWN | SDL_WINDOW_BORDERLESS | SDL_WINDOW_ALLOW_HIGHDPI;

    // Anchored near the top of the screen's *usable* area (excludes the
    // menu bar/dock), not vertically centered: the three windows stack to
    // a real height, and centering a tall stack pushes its bottom half (the
    // playlist window, in practice) down past the bottom of the screen -
    // exactly the "can't reach the playlist" symptom this was reported as.
    SDL_Rect usable{};
    SDL_GetDisplayUsableBounds(0, &usable);
    const int startX = usable.x + std::max(0, (usable.w - ScaledDim(app::layout::kWindowW, scale)) / 2);
    const int startY = usable.y + 8;

    SDL_Window* window = SDL_CreateWindow("X.MaD Player Revival", startX, startY,
                                           ScaledDim(app::layout::kWindowW, scale),
                                           ScaledDim(app::layout::kWindowH, scale), kWindowFlags);
    if (!window) {
        std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << "\n";
        return 1;
    }

#ifdef __APPLE__
    // Explicitly activates the app (SetDockIconVisible(true) already does
    // this as a side effect of its normal tray-restore job, reused here
    // rather than adding a near-duplicate function) - a plain SDL-created
    // window isn't guaranteed to leave the app genuinely "active" the way
    // a normal Finder/Dock launch does, and MPRemoteCommandCenter/
    // MPNowPlayingInfoCenter registration made before that appears not to
    // stick reliably (see media_remote.h).
    app::SetDockIconVisible(true);
#endif

    // Software renderer: these are simple 2D sprite blits (no need for GPU
    // acceleration), and it draws into a CPU-side buffer that doesn't
    // depend on a real compositor/display surface being attached - works
    // the same whether there's a real window on screen or not.
    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    if (!renderer) {
        std::cerr << "SDL_CreateRenderer failed: " << SDL_GetError() << "\n";
        return 1;
    }
    ApplyHiDpiRenderScale(renderer, app::layout::kWindowW, app::layout::kWindowH);

    int mainX, mainY;
    SDL_GetWindowPosition(window, &mainX, &mainY);
    const WindowLayout initialLayout = ComputeWindowLayout(scale, mainX, mainY);

    // Back to a single vertical column (Main / EQ / Playlist, Playlist at
    // the bottom) - side-by-side was a stopgap for a too-tall stack at 2x
    // scale; at native 1x scale (see `scale` above) the full column is
    // short enough to fit, and stacked reads better than side-by-side.
    // (150%, the largest scale this app's UI-scale setting offers, still
    // fits: (155+130+140)*1.5 = 638px, well under a typical usable
    // display height - no side-by-side fallback needed.)
    SDL_Window* eqWindow =
        SDL_CreateWindow("X.MaD Player Revival - Equalizer", initialLayout.eq.x, initialLayout.eq.y,
                          initialLayout.eq.w, initialLayout.eq.h, kWindowFlags);
    if (!eqWindow) {
        std::cerr << "SDL_CreateWindow (eq) failed: " << SDL_GetError() << "\n";
        return 1;
    }
    SDL_Renderer* eqRenderer = SDL_CreateRenderer(eqWindow, -1, SDL_RENDERER_SOFTWARE);
    if (!eqRenderer) {
        std::cerr << "SDL_CreateRenderer (eq) failed: " << SDL_GetError() << "\n";
        return 1;
    }
    ApplyHiDpiRenderScale(eqRenderer, app::layout::kEqWindowW, app::layout::kEqWindowH);

    SDL_Window* plWindow =
        SDL_CreateWindow("X.MaD Player Revival - Playlist", initialLayout.playlist.x, initialLayout.playlist.y,
                          initialLayout.playlist.w, initialLayout.playlist.h, kWindowFlags);
    if (!plWindow) {
        std::cerr << "SDL_CreateWindow (playlist) failed: " << SDL_GetError() << "\n";
        return 1;
    }
    SDL_Renderer* plRenderer = SDL_CreateRenderer(plWindow, -1, SDL_RENDERER_SOFTWARE);
    if (!plRenderer) {
        std::cerr << "SDL_CreateRenderer (playlist) failed: " << SDL_GetError() << "\n";
        return 1;
    }
    ApplyHiDpiRenderScale(plRenderer, app::layout::kPlaylistWindowW, app::layout::kPlaylistWindowH);

    // Info window: to the right of the Main/EQ/Playlist column rather than
    // in it - unlike those three, it's not part of the docking/stacking
    // requirement (an on-demand popup, not an always-visible window), so it
    // doesn't need a stacking slot of its own. Starts hidden; the Info
    // button toggles it (see toggleSecondaryWindow below).
    SDL_Window* infoWindow =
        SDL_CreateWindow("X.MaD Player Revival - Info", initialLayout.info.x, initialLayout.info.y,
                          initialLayout.info.w, initialLayout.info.h, kWindowFlags);
    if (!infoWindow) {
        std::cerr << "SDL_CreateWindow (info) failed: " << SDL_GetError() << "\n";
        return 1;
    }
    SDL_Renderer* infoRenderer = SDL_CreateRenderer(infoWindow, -1, SDL_RENDERER_SOFTWARE);
    if (!infoRenderer) {
        std::cerr << "SDL_CreateRenderer (info) failed: " << SDL_GetError() << "\n";
        return 1;
    }
    ApplyHiDpiRenderScale(infoRenderer, app::layout::kInfoWindowW, app::layout::kInfoWindowH);
    SDL_HideWindow(infoWindow);

    // About window (TODO: "create about page linked to the japanese
    // character click ... with this logo ... and version"). Same
    // popup-not-docked treatment as Info, stacked below it in the same
    // column to its right rather than overlapping it outright. Opened by
    // clicking the shock/LOGOJAP icon on Main (see kShockX's hit-test
    // below) - see layout.h's kAboutWindow* comment for why that icon.
    SDL_Window* aboutWindow =
        SDL_CreateWindow("X.MaD Player Revival - About", initialLayout.about.x, initialLayout.about.y,
                          initialLayout.about.w, initialLayout.about.h, kWindowFlags);
    if (!aboutWindow) {
        std::cerr << "SDL_CreateWindow (about) failed: " << SDL_GetError() << "\n";
        return 1;
    }
    SDL_Renderer* aboutRenderer = SDL_CreateRenderer(aboutWindow, -1, SDL_RENDERER_SOFTWARE);
    if (!aboutRenderer) {
        std::cerr << "SDL_CreateRenderer (about) failed: " << SDL_GetError() << "\n";
        return 1;
    }
    ApplyHiDpiRenderScale(aboutRenderer, app::layout::kAboutWindowW, app::layout::kAboutWindowH);
    SDL_HideWindow(aboutWindow);

    // Tracks "does the user *want* this window shown" independent of SDL's
    // own SDL_WINDOW_HIDDEN flag - which turns out to also read true for a
    // merely-*minimized* window on this platform (confirmed directly: a
    // window minimized via SDL_MinimizeWindow reports both
    // SDL_WINDOW_MINIMIZED and SDL_WINDOW_HIDDEN set). The minimize/
    // restore cascade below needs to tell "the user closed this on
    // purpose, leave it alone" apart from "this is just minimized right
    // now, it should come back" - and SDL_WINDOW_HIDDEN alone can't do
    // that, since both cases read identically. An earlier version relied
    // on that flag directly, which meant restoring any window *other*
    // than the one that triggered the cascade never worked (they all
    // looked "hidden" to the flag check, since they were minimized) - the
    // user caught this in real use after it was reported fixed.
    bool eqUserVisible = true, plUserVisible = true, infoUserVisible = false, aboutUserVisible = false;

    // Our own "is this window currently minimized" bookkeeping, separate
    // again from any SDL flag - real-machine testing showed
    // SDL_WINDOWEVENT_RESTORED doesn't reliably arrive when a *secondary*
    // window's Dock icon is clicked (only Main's restore was ever
    // observed to fire it), so the cascade also reacts to
    // SDL_WINDOWEVENT_FOCUS_GAINED, which does fire whenever any Dock
    // icon is clicked, minimized or not. Reacting to every focus-gain
    // would incorrectly un-hide windows the user closed on purpose just
    // because they clicked something else, so it's gated on "did we
    // think this window was minimized" - only a window this code itself
    // marked minimized can trigger the cascade back out.
    bool mainMinimized = false, eqMinimized = false, plMinimized = false, infoMinimized = false,
         aboutMinimized = false;
#ifdef __APPLE__
    // Separate from the per-window *Minimized bools above: those can each
    // flip multiple times during one user minimize/restore action (every
    // cascaded window fires its own real MINIMIZED/RESTORED event), but the
    // Dock-hide/menu-bar-show swap must happen exactly once per action - on
    // the transition into "something is minimized" and back out. Guarded on
    // this bool at the two call sites below rather than on any per-window
    // state.
    bool appHiddenToTray = false;
#endif

    // Shared by the PL/EQ toggle buttons on Main and by the Info button
    // (defined this early, rather than down with the rest of the event
    // handling, so handleUtilityPress below can already call it).
    auto toggleSecondaryWindow = [&](SDL_Window* w, bool& userVisible, bool& minimized) {
        if (userVisible) {
            userVisible = false;
            SDL_HideWindow(w);
        } else {
            userVisible = true;
            minimized = false; // showing it directly - not "restored from minimize" bookkeeping's business
            SDL_ShowWindow(w);
            SDL_RaiseWindow(w);
        }
    };

    // "Maximize" (see kLiteX's comment in layout.h - this repurposes the
    // existing LitePic triangle, not a new control): shows EQ+Playlist
    // together if either is currently hidden, or hides both together if
    // both are already shown - one combined action instead of the two
    // individual PL/EQ toggle buttons. "Maximized" isn't a separately
    // tracked flag; it's derived from whether both happen to be visible
    // right now, so this can never drift out of sync with reality (e.g.
    // after the user closes just one of them by hand).
    auto toggleMaximize = [&]() {
        const bool bothVisible = plUserVisible && eqUserVisible;
        if (bothVisible) {
            plUserVisible = false;
            eqUserVisible = false;
            SDL_HideWindow(plWindow);
            SDL_HideWindow(eqWindow);
        } else {
            plUserVisible = true;
            eqUserVisible = true;
            plMinimized = false; // showing directly, same reasoning as toggleSecondaryWindow
            eqMinimized = false;
            SDL_ShowWindow(plWindow);
            SDL_ShowWindow(eqWindow);
            SDL_RaiseWindow(plWindow);
            SDL_RaiseWindow(eqWindow);
        }
    };

    app::Skin skin;
    try {
        skin = app::Skin::Load(assetDir);
    } catch (const std::exception& e) {
        std::cerr << "Failed to load skin from '" << assetDir << "': " << e.what() << "\n";
        std::cerr << "(pass the asset directory as argv[1] if running from elsewhere)\n";
        return 1;
    }

    gfx::BitmapFont font(skin.displayFont);

    audio::Engine engine;

#ifdef __APPLE__
    // Snapshots every Effects-menu-backed toggle on Engine into the struct
    // SetEffectsMenuChecked expects - single source of truth used by every
    // call site below (initial sync, each toggle lambda, tray entry) so
    // they can't drift out of sync with each other.
    auto effectsMenuState = [&]() {
        return app::EffectsMenuState{engine.XSound(), engine.ReverbOn(), engine.SaturationOn(),
                                      engine.CompressionOn(), engine.ChorusOn()};
    };
#endif

#ifdef __APPLE__
    // Enter/exit tray state by hiding/showing windows directly, never by
    // routing Main through a real OS miniaturize (SDL_MinimizeWindow):
    // that was tried first and broke restoring Main specifically (EQ/PL,
    // which were only ever plain-hidden, restored fine; Main, which had
    // actually been miniaturized by the click handler before this code
    // forced it hidden out from under that state, then didn't reliably
    // come back via SDL_ShowWindow - SDL's Cocoa backend tracks
    // miniaturized-vs-hidden internally via NSWindow delegate callbacks,
    // and calling orderOut: on an already-miniaturized window doesn't
    // fire windowDidDeminiaturize:, leaving that bookkeeping stale).
    // Called directly from the minimize button's click handler (bypassing
    // SDL_MinimizeWindow/the MINIMIZED event entirely for the normal
    // path) and from the menu-bar icon's synthetic restore event.
    // Defined here (needs `engine`, declared just above, for the xSound
    // checkmark sync below) rather than alongside appHiddenToTray up
    // with the rest of this state.
    auto enterAppTray = [&]() {
        if (appHiddenToTray) return;
        appHiddenToTray = true;
        mainMinimized = true;
        SDL_HideWindow(window);
        if (eqUserVisible) {
            eqMinimized = true;
            SDL_HideWindow(eqWindow);
        }
        if (plUserVisible) {
            plMinimized = true;
            SDL_HideWindow(plWindow);
        }
        if (infoUserVisible) {
            infoMinimized = true;
            SDL_HideWindow(infoWindow);
        }
        if (aboutUserVisible) {
            aboutMinimized = true;
            SDL_HideWindow(aboutWindow);
        }
        // Add the new access point before removing the old one, so
        // there's never a moment with neither a Dock icon nor a
        // menu-bar icon available to click.
        app::ShowMenuBarIcon(kTrayRestoreEventType, kUiScaleEventType, kEffectsEventType, kTrayAboutEventType);
        // ShowMenuBarIcon is idempotent (a no-op past the first call),
        // so it only actually builds the tray popup's own View/Effects
        // items the first time the app is ever minimized in this run -
        // freshly built items default to unchecked, so without this
        // they'd stay wrong forever if the scale/xSound state at that
        // moment wasn't the default (e.g. xSound already toggled on
        // before the first minimize). Harmless to call every time -
        // just re-applies the same state to items already in sync.
        app::SetUiScaleMenuChecked(uiScalePercent);
        app::SetEffectsMenuChecked(effectsMenuState());
        app::SetDockIconVisible(false);
    };
    auto exitAppTray = [&]() {
        if (!appHiddenToTray) return;
        appHiddenToTray = false;
        // Reactivate *before* showing/raising anything below: an
        // Accessory-policy app isn't guaranteed to come to the
        // foreground just because a window gets ordered front.
        app::SetDockIconVisible(true);
        // Fixed order (About, Info, Playlist, EQ, then Main last) -
        // matches the pre-existing restore-order convention elsewhere in
        // this file so the resulting window stacking looks the same.
        if (aboutMinimized) {
            aboutMinimized = false;
            SDL_ShowWindow(aboutWindow);
        }
        if (infoMinimized) {
            infoMinimized = false;
            SDL_ShowWindow(infoWindow);
        }
        if (plMinimized) {
            plMinimized = false;
            SDL_ShowWindow(plWindow);
        }
        if (eqMinimized) {
            eqMinimized = false;
            SDL_ShowWindow(eqWindow);
        }
        mainMinimized = false;
        SDL_ShowWindow(window);
        SDL_RaiseWindow(window);
        app::HideMenuBarIcon();
    };
#endif

    app::Playlist playlist;

    // Session persistence (TODO: "save settings on exit"). Resuming last
    // session's playlist/volume/spec-mode/playing-state only engages when
    // nothing else already said what to open - an explicit --playlist/
    // --play/positional track always wins, matching how opening a specific
    // file in any media player takes priority over restoring the last
    // session. Also never engages during a --dump-*-frame headless
    // verification run: those need a deterministic starting point, not
    // whatever a real session left on disk (see app/session.h).
    const bool explicitPlaylistSource = !playlistFile.empty() || !playPath.empty() || !positionalTracks.empty();
    const bool dumpingAnyFrame = !dumpFramePath.empty() || !dumpPlaylistFramePath.empty() ||
                                  !dumpEqFramePath.empty() || !dumpInfoFramePath.empty() ||
                                  !dumpAboutFramePath.empty();
    const bool resumeSession = !explicitPlaylistSource && !dumpingAnyFrame;
    app::Settings sessionSettings;
    if (resumeSession) {
        playlist.LoadM3U(app::SessionPlaylistPath()); // no warning if missing - first run has none yet
        if (app::LoadSettingsFile(app::SettingsFilePath(), sessionSettings)) {
            std::cout << "Resumed session: specMode=" << sessionSettings.specMode
                      << " volume=" << sessionSettings.volumePercent
                      << " wasPlaying=" << sessionSettings.wasPlaying
                      << " wasPaused=" << sessionSettings.wasPaused << " index=" << sessionSettings.currentIndex
                      << " positionSeconds=" << sessionSettings.positionSeconds
                      << " xSound=" << sessionSettings.xSound << " eqPreset=" << sessionSettings.eqPreset
                      << " visPanel=" << sessionSettings.visPanel << " playlistSize=" << playlist.size() << "\n";
        }
    }

    if (!playlistFile.empty() && !playlist.LoadM3U(playlistFile)) {
        std::cerr << "warning: could not read playlist '" << playlistFile << "'\n";
    }
    if (!playPath.empty()) playlist.Add(playPath);
    for (auto& t : positionalTracks) playlist.Add(t);

    if (!playlist.empty()) {
        if (resumeSession) {
            const int idx = std::clamp(sessionSettings.currentIndex, 0, static_cast<int>(playlist.size()) - 1);
            playlist.SetCurrentIndex(idx);
            // Whether a track auto-resumes *playing* on relaunch is gated
            // on wasPlaying alone (mirrors the original's GestisciPosFrm
            // LASTMP3 resume) - but whether it reopens/seeks *at all* is
            // gated on wasPlaying-or-wasPaused. A plain Stop (the original's
            // "don't resume" case) still doesn't reopen anything; a Pause
            // used to fall into that same "don't reopen" bucket too, which
            // is the bug the user reported ("pause then close loses the
            // position, and Pause on relaunch does nothing since nothing's
            // loaded to resume") - Paused means "come back to this", not
            // "done with this", so it now reopens and seeks like Playing
            // does, just without auto-starting playback. No clamping needed
            // here - SeekSeconds already clamps its lower bound, and a
            // stale saved position past a (possibly since-edited) track's
            // actual end degrades gracefully through the normal
            // end-of-track handling.
            if (sessionSettings.wasPlaying || sessionSettings.wasPaused) {
                engine.Open(playlist.at(static_cast<size_t>(idx)));
                engine.SeekSeconds(sessionSettings.positionSeconds);
                // Open() always starts playback - undo that immediately if
                // the saved state was Paused, not Playing.
                if (!sessionSettings.wasPlaying) engine.Pause();
            }
        } else {
            playlist.SetCurrentIndex(0);
            if (!engine.Open(playlist.at(0))) {
                std::cerr << "warning: could not open/play '" << playlist.at(0) << "'\n";
            }
        }
    }
    if (resumeSession) {
        engine.SetVolume(std::clamp(sessionSettings.volumePercent, 0, 100) / 100.0f);
        engine.SetXSound(sessionSettings.xSound);
        engine.SetReverbOn(sessionSettings.reverb);
        engine.SetSaturationOn(sessionSettings.saturation);
        engine.SetCompressionOn(sessionSettings.compression);
        engine.SetChorusOn(sessionSettings.chorus);
        // TODO: "EQ mode not saved". Restoring the actual band gains here
        // (not just the preset index, see eqCurrentPreset's init below) is
        // what makes the restored EQ audibly correct even after a manual
        // tweak that isn't any of the five fixed presets.
        for (int b = 0; b < audio::Equalizer::kBands; ++b) {
            engine.SetEqBand(b, std::clamp(sessionSettings.eqBands[static_cast<size_t>(b)], -127, 127));
        }
    }

    SDL_Texture* texBack = UploadTexture(renderer, skin.btnBack);
    SDL_Texture* texPlay = UploadTexture(renderer, skin.btnPlay);
    SDL_Texture* texStop = UploadTexture(renderer, skin.btnStop);
    SDL_Texture* texNext = UploadTexture(renderer, skin.btnNext);
    SDL_Texture* texPause = UploadTexture(renderer, skin.btnPause);
    SDL_Texture* texEject = UploadTexture(renderer, skin.btnEject);
    SDL_Texture* texInfo = UploadTexture(renderer, skin.btnInfo);
    SDL_Texture* texVol = UploadTexture(renderer, skin.btnVol);
    SDL_Texture* texMute = UploadTexture(renderer, skin.btnMute);
    SDL_Texture* texSpecMode = UploadTexture(renderer, skin.btnSpecMode);
    SDL_Texture* texLogo = UploadTexture(renderer, skin.logo);
    SDL_Texture* texLite = UploadTexture(renderer, skin.lite);
    SDL_Texture* texShock = UploadTexture(renderer, skin.shock);
    SDL_Texture* texMinimize = UploadTexture(renderer, skin.minimize);
    // Uploaded against aboutRenderer, not renderer - textures are bound to
    // the SDL_Renderer they were created with, and these are only ever
    // drawn into the About window.
    SDL_Texture* texAboutIcon = UploadTexture(aboutRenderer, skin.aboutIcon);
    SDL_Texture* texAboutZLogo = UploadTexture(aboutRenderer, skin.aboutZLogo);
    // VisPanel::IdleLogo - drawn stretched into the analyzer's box (see
    // DrawTextureFit), matching the original's ImgLogo.Stretch = True.
    SDL_Texture* texIdleLogo = UploadTexture(renderer, skin.idleLogo);
    SDL_Texture* texVolUp = UploadTexture(renderer, skin.volUp);
    SDL_Texture* texVolDown = UploadTexture(renderer, skin.volDown);
    SDL_Texture* texModeNone = UploadTexture(renderer, skin.modeNone);
    SDL_Texture* texModeMono = UploadTexture(renderer, skin.modeMono);
    SDL_Texture* texModeStereo = UploadTexture(renderer, skin.modeStereo);
    SDL_Texture* texModeXSound = UploadTexture(renderer, skin.modeXSound);

    // texMarquee is rebuilt per frame in drawFrame (title changes as the
    // playlist advances), unlike the other static textures here.
    //
    // Two real screenshots of the original both confirm the asterisks
    // belong on the STATUS line, not the title - matches Form_Load's
    // `xmDisplay(1).xCaption = "*** v" & ... & " ***"` exactly (xmDisplay(0),
    // the title, gets no asterisks anywhere in the source). An earlier pass
    // here had these backwards. The version number itself was changed to
    // this project's own (app/version.h) at the user's request, after an
    // initial pass left it as the original's real "V1.0.378" for
    // screenshot fidelity - the user confirmed they'd rather see this
    // project's version consistently everywhere than keep that one
    // faithful holdout. The original's own release codename ("STABLE
    // GAMERA") was likewise replaced, at the user's request, with this
    // project's own ("BETA GOJIRA", bumped from "ALPHA GOJIRA" alongside
    // the 1.0.0 version bump) - same kaiju theme, own naming.
    SDL_Texture* texStatus = RenderTextTexture(
        renderer, font, std::string("*** V") + app::kVersion + " BETA GOJIRA ***", app::layout::kDisplayFieldW);

    // LCD readout cluster. "MODE" is a genuinely static caption (Form_Load
    // hardcodes lblMode's text the same way in the original). Duration,
    // Freq, BitRate and Vol are all real, computed live in drawFrame below
    // - BitRate used to be a permanently-hardcoded "128" placeholder
    // (TODO: "the Khz and bit rate labels are fake"); see
    // ComputeAvgBitrateKbps's comment for why it's an average rather than
    // a true running bitrate.
    SDL_Texture* texModeLabel = RenderTextTexture(renderer, font, "MODE", app::layout::kModeLabelW);

    using namespace app::layout;

    // Real spectrum analyzer, fed from Engine::GetVisSnapshot (captured in
    // the audio callback, so it reflects what's actually playing right
    // now). One persistent instance reused every frame - constructing it
    // allocates internal buffers sized to the FFT window.
    constexpr int kFftPoints = audio::Engine::kVisSnapshotFrames; // 1024
    constexpr int kNumBars = 29; // matches the analyzer's ~89px width at 3px/bar
    dsp::SpectrumAnalyzer spectrumAnalyzer(kFftPoints, dsp::Window::Hanning);
    std::array<float, kNumBars> barLevels{}; // raw target level this frame, 0..1
    float peakL = 0.0f, peakR = 0.0f;

    // Per-bar animation state for the four bar-based visualizer modes
    // (PeakFalls/NoPeakFalls/PeakNoFalls/FadeFft), all in analyzer-height
    // pixel units (0..kAnalyzerH) - mirrors volume.bas's BitBltSpec (W/P
    // arrays) and DisgnaxmMP3FFT (Spec/Spec2 arrays). Persists across
    // frames so the fall/decay animation is continuous, not recomputed
    // from scratch each frame.
    std::array<float, kNumBars> barHeightPx{};  // BitBltSpec's W(I): eased/falling bar height
    std::array<float, kNumBars> peakHeightPx{}; // BitBltSpec's P(I): slow-creeping peak-hold indicator
    std::array<float, kNumBars> fadeFillPx{};   // DisgnaxmMP3FFT's Spec(I): fast-reactive fill
    std::array<float, kNumBars> fadeTrailPx{};  // DisgnaxmMP3FFT's Spec2(I): slower trailing peak line

    // CardioOSC (VisPanel::CardioOSC) - Sin(left)/Des(right) scrolling
    // oscilloscope traces (DisegnaCardioOSC's picCardioSin/Des, a PSet
    // scroll: one new point plotted per update, nothing else cleared,
    // until the buffer's width is reached and it clears/restarts -
    // IndiceOsc in the original). Real alpha here (not the bitmap-font's
    // color-key trick), since these are built directly in code rather
    // than loaded from an always-opaque BMP: unlit pixels start at
    // alpha=0 so the scattered-dot look composites correctly over
    // whatever's already drawn. The VU bars alongside these reuse
    // peakL/peakR + gfx::LevelGradientColor directly - no extra state
    // needed for those.
    constexpr int kCardioVuW = 4;
    constexpr int kCardioGapPx = 1;
    constexpr int kCardioHalfW = kAnalyzerW / 2;
    constexpr int kCardioTraceW = kCardioHalfW - kCardioVuW - kCardioGapPx;
    gfx::Image cardioSinBuf, cardioDesBuf;
    cardioSinBuf.width = cardioDesBuf.width = kCardioTraceW;
    cardioSinBuf.height = cardioDesBuf.height = kAnalyzerH;
    cardioSinBuf.rgba.assign(static_cast<size_t>(kCardioTraceW) * kAnalyzerH * 4, 0);
    cardioDesBuf.rgba.assign(static_cast<size_t>(kCardioTraceW) * kAnalyzerH * 4, 0);
    int cardioScrollIdx = 0;
    auto plotCardioColumn = [&](gfx::Image& buf, float level01) {
        const int y = std::clamp(buf.height - 1 - static_cast<int>(level01 * (buf.height - 1)), 0, buf.height - 1);
        uint8_t* p = buf.pixel(cardioScrollIdx, y);
        p[0] = 0x3d;
        p[1] = 0xff;
        p[2] = 0x74;
        p[3] = 255;
    };

    // FallsVel/noFallsVel/PointFalls ported literally from FunzioniGlobali.bas's
    // defaults (GetINI("VISUALIZATION","FALLSVEL"/"NOFALLSVEL"/"POINTFALLS")):
    // these are pixel-per-redraw-tick constants tuned against
    // AltezzaSpectrumDisplay=23, which is coincidentally identical to our own
    // kAnalyzerH=23 - both trace back to the same control height - so they're
    // used as-is rather than rescaled. FadeFft's decay rates are NOT ported
    // literally: the original's Spec/Spec2 arithmetic operates on values in
    // an inconsistent, hard-to-reconstruct scale (small magnitudes decayed by
    // huge per-frame steps, effectively resetting almost every frame) that
    // doesn't translate to a meaningful pixel rate - these two are a
    // clean-room approximation of the same *shape* (fast snap-to-level fill,
    // slower trailing peak line), not ported values.
    constexpr float kFallsVelSlow = 0.3f;  // PeakFalls / NoPeakFalls
    constexpr float kFallsVelFast = 0.5f;  // PeakNoFalls ("noFalls" - misleadingly named; still eased, just quicker)
    constexpr float kPeakOffsetPx = 5.0f;  // PointFalls: gap above the bar when the peak indicator resets
    constexpr float kPeakCreepPx = 0.05f;  // how slowly the peak indicator settles otherwise
    constexpr float kFadeFillDecayPx = 1.4f;
    constexpr float kFadeTrailDecayPx = 0.2f;

    // Log-scale bin grouping (bar i covers bins [edges[i], edges[i+1])) -
    // standard spectrum-analyzer bucketing: fine resolution at the low end,
    // coarse at the high end, without needing to know the actual sample
    // rate (this groups by bin index, not Hz).
    std::array<int, kNumBars + 1> barBinEdges{};
    {
        constexpr int kFftBins = kFftPoints / 2;
        for (int i = 0; i <= kNumBars; ++i) {
            const double t = static_cast<double>(i) / kNumBars;
            int edge = static_cast<int>(std::lround(std::pow(static_cast<double>(kFftBins), t)));
            barBinEdges[static_cast<size_t>(i)] = std::clamp(edge, 1, kFftBins - 1);
        }
        for (int i = 1; i <= kNumBars; ++i) {
            if (barBinEdges[static_cast<size_t>(i)] <= barBinEdges[static_cast<size_t>(i - 1)]) {
                barBinEdges[static_cast<size_t>(i)] = barBinEdges[static_cast<size_t>(i - 1)] + 1;
            }
        }
    }

    // Transport row hit-test table, in the same left-to-right order they're
    // drawn (Back, Play, Stop, Next, Pause, Eject - the runtime-reflowed
    // order, see layout.h).
    struct TransportButton {
        SDL_Rect rect;
        TransportAction action;
    };
    std::array<TransportButton, 6> transportButtons;
    {
        const TransportAction order[6] = {TransportAction::Back,  TransportAction::Play,
                                           TransportAction::Stop,  TransportAction::Next,
                                           TransportAction::Pause, TransportAction::Eject};
        for (int i = 0; i < 6; ++i) {
            transportButtons[i] = {SDL_Rect{kTransportX0 + i * kTransportBtnW, kTransportY, kTransportBtnW,
                                             kTransportBtnH},
                                    order[i]};
        }
    }
    int pressedButton = -1; // index into transportButtons, -1 = none held
    // Tooltip support (user request: "put tooltips on the visualizations
    // so I can tell you which ones need refining") - real xmDisplay-style
    // ToolTipText values from the original (DisegaSpectrum/xmp.frm), not
    // invented labels, so feedback can reference the actual mode name.
    // -1,-1 = not currently hovering the main window at all.
    int mainMouseX = -1, mainMouseY = -1;

    // Utility row (ShowVol/Mute/SpecMode/Info - CommandImg indices 7/8/9/6).
    // Only SpecMode does anything real for now; the other three stay
    // decorative (documented, not silently dropped).
    enum class UtilityAction { ShowVol, Mute, SpecMode, Info };
    struct UtilityButton {
        SDL_Rect rect;
        UtilityAction action;
    };
    const std::array<UtilityButton, 4> utilityButtons = {
        UtilityButton{SDL_Rect{kShowVolX, kShowVolY, kTransportBtnW, kTransportBtnH}, UtilityAction::ShowVol},
        UtilityButton{SDL_Rect{kMuteX, kMuteY, kTransportBtnW, kTransportBtnH}, UtilityAction::Mute},
        UtilityButton{SDL_Rect{kSpecModeX, kSpecModeY, kTransportBtnW, kTransportBtnH}, UtilityAction::SpecMode},
        UtilityButton{SDL_Rect{kInfoX, kInfoY, kTransportBtnW, kTransportBtnH}, UtilityAction::Info},
    };
    int pressedUtility = -1;
    bool volDragging = false; // true while the volume slider thumb is being dragged
    bool seekDragging = false; // true while the seek bar thumb is being dragged

    // Mirrors modVisualMusic.bas's GlobalSpectrumeMode cycling on the
    // SpecMode button - all six of the original's sub-modes, in the same
    // order (PeakFalls=0/noPeakFalls=1/PeaknoFalls=2/noPeaknoFalls=3/OSC=4/
    // StereoOSC=5). PeakFalls/NoPeakFalls/PeakNoFalls share one renderer
    // (BitBltSpec) differing only in the peak-hold dot and fall speed;
    // FadeFft is DisgnaxmMP3FFT, a genuinely different per-column-gradient
    // renderer - see the animation-state comment above for what's ported
    // literally vs. approximated.
    enum class VisMode { PeakFalls, NoPeakFalls, PeakNoFalls, FadeFft, Oscilloscope, StereoOscilloscope };
    VisMode visMode = resumeSession ? static_cast<VisMode>(std::clamp(sessionSettings.specMode, 0, 5))
                                     : VisMode::PeakFalls;

    // TODO: "visualizations missing from the original". The real xmp.frm
    // shares ONE box (the analyzer's) between three completely separate
    // displays, cycled by clicking anywhere in it - analyzer_Click shows
    // ImgLogo, ImgLogo_Click shows CardioOSC, picCardioSin/Des_Click shows
    // the analyzer again (xmp.frm). VisMode above is only the analyzer's
    // OWN 6 sub-modes (SpecMode button) - a completely different, nested
    // cycle that only matters while VisPanel::Analyzer is showing.
    enum class VisPanel { Analyzer, IdleLogo, CardioOSC };
    VisPanel visPanel = resumeSession
                             ? static_cast<VisPanel>(std::clamp(sessionSettings.visPanel, 0, 2))
                             : VisPanel::Analyzer;

    // Mirrors mnuXSound.Checked (Menu.frm) - a toggle from the original
    // that overrides the Stereo/Mono mode icon to "XSound" whenever it's
    // on, independent of the actual channel count, and (via mXSrnd in
    // modxmMP3Interface.bas) enables its "Surround" DSP flag - a separate
    // "Normalize"/Level feature also lives behind XSound in the original
    // but isn't part of this toggle there either, so it's out of scope
    // here too. Bound to the same "x" key the original's global hotkey
    // used (hotkeys.bas, Case Asc("x")) - window-focused rather than
    // system-wide, the same simplification Escape-to-quit already makes
    // - and, on macOS, to the Effects menu's xSound item (main_menu.h).
    // The actual audio effect lives on Engine (SetXSound/XSound - see
    // audio/stereo_widen.h for what it substitutes and why); this just
    // toggles it, so `engine.XSound()` is the single source of truth
    // rather than a separate local flag.
    auto toggleXSound = [&]() {
        engine.SetXSound(!engine.XSound());
#ifdef __APPLE__
        app::SetEffectsMenuChecked(effectsMenuState());
#endif
    };
    // Mirrors toggleXSound above for each of the other fixed-parameter
    // Effects-menu toggles - see audio/{reverb,saturation,compressor,
    // chorus}.h for what each substitutes/implements and why. Menu-only
    // (no keyboard shortcut, no debug CLI flag): unlike xSound these are
    // new additions with no prior hotkey to preserve, and DSP correctness
    // is already covered by their own unit tests.
    auto toggleReverb = [&]() {
        engine.SetReverbOn(!engine.ReverbOn());
#ifdef __APPLE__
        app::SetEffectsMenuChecked(effectsMenuState());
#endif
    };
    auto toggleSaturation = [&]() {
        engine.SetSaturationOn(!engine.SaturationOn());
#ifdef __APPLE__
        app::SetEffectsMenuChecked(effectsMenuState());
#endif
    };
    auto toggleCompression = [&]() {
        engine.SetCompressionOn(!engine.CompressionOn());
#ifdef __APPLE__
        app::SetEffectsMenuChecked(effectsMenuState());
#endif
    };
    auto toggleChorus = [&]() {
        engine.SetChorusOn(!engine.ChorusOn());
#ifdef __APPLE__
        app::SetEffectsMenuChecked(effectsMenuState());
#endif
    };
#ifdef __APPLE__
    app::SetEffectsMenuChecked(effectsMenuState()); // reflect resumed Effects state immediately
#endif

    auto handleUtilityPress = [&](UtilityAction action) {
        // Matches CommandImg(9).Enabled = analyzer.Visible - the SpecMode
        // button only cycles the analyzer's own 6 sub-modes while the
        // analyzer is actually the panel showing; a no-op during IdleLogo/
        // CardioOSC, same as the original disabling the button outright.
        if (action == UtilityAction::SpecMode) {
            if (visPanel == VisPanel::Analyzer) {
                visMode = static_cast<VisMode>((static_cast<int>(visMode) + 1) % 6);
            }
        } else if (action == UtilityAction::Mute) {
            engine.SetMuted(!engine.IsMuted());
        } else if (action == UtilityAction::Info) {
            toggleSecondaryWindow(infoWindow, infoUserVisible, infoMinimized);
        }
        // ShowVol: no engine-side effect yet.
    };

    // Step per arrow click - proportionally similar to the EQ arrows' ~5% of
    // full range (13/254). The original's xmSlide arrows instead auto-repeat
    // by 1 (of 100) per iteration of a MouseDown-held loop; a fixed per-click
    // step is the simplification here, same spirit as handleEqSliderClickAt's
    // click-to-jump vs. the original's continuous drag.
    constexpr float kVolStep = 0.05f;

    // Click-to-jump on the track, matching handleEqSliderClickAt's approach;
    // additionally supports live drag-follow via SDL_MOUSEMOTION below
    // (unlike the EQ sliders) since a single always-visible volume slider is
    // cheap to make fully draggable. Manually touching the slider always
    // clears mute - the original leaves Mute latched even after a manual
    // vsGhost_Change, which reads as a quirk (slider moves but stays
    // "muted") rather than intended behavior; clearing it here is a small,
    // deliberate divergence for a less surprising UI.
    auto handleVolSliderClickAt = [&](int ly) {
        const int top = kVolSliderY + kVolArrowSize;
        const int bottom = kVolSliderY + kVolSliderH - kVolArrowSize;
        const int clamped = std::clamp(ly, top, bottom);
        const float frac = static_cast<float>(clamped - top) / static_cast<float>(bottom - top); // 0=top,1=bottom
        engine.SetVolume(1.0f - frac); // top = max volume, matching xmsVol.xValue's inverted sense
        engine.SetMuted(false);
    };

    // Click-to-jump + drag-follow on the seek bar (lnPosizione/PicPosizione),
    // same shape as handleVolSliderClickAt above. No-op with nothing loaded
    // to seek. Uses the exact same travel math the render code already uses
    // for the thumb's x position (see the "seek track" block in drawFrame
    // below) so clicking where the thumb visually sits lands on the
    // position it visually represents.
    auto handleSeekBarClickAt = [&](int lx) {
        if (engine.channels() == 0 || engine.durationSeconds() <= 0.0) return;
        const int seekTravel = (kSeekX1 - kSeekX0) - 8;
        const float frac = std::clamp(static_cast<float>(lx - kSeekX0) / static_cast<float>(seekTravel), 0.0f, 1.0f);
        engine.SeekSeconds(frac * engine.durationSeconds());
    };

    // Opens playlist[idx] and starts it playing, keeping playlist's
    // currentIndex_ in sync (mirrors PlayStream's IndiceGlobalissimo update).
    // Bluetooth/Control Center Now Playing info (macOS-only) needs the
    // current track's display name, which needs getPlaylistDisplayName -
    // not yet declared this far up. Same deferred-std::function trick as
    // onAddFilesRequested/onAddFolderRequested below, assigned its real
    // body once getPlaylistDisplayName exists. No-op on non-Darwin builds, where
    // this stays the do-nothing default forever.
    std::function<void(bool)> notifyNowPlayingChanged = [](bool) {};

    // Same deferred-std::function trick, same reason: applies the
    // just-opened track's per-song EQ bands (perSongEqEnabled/
    // perSongEqBands aren't declared until the EQ window's state further
    // down). No-op (default) until that real body is assigned.
    std::function<void()> applyPerSongEqForCurrentTrack = [] {};

    auto openPlaylistIndex = [&](int idx) {
        if (idx < 0 || static_cast<size_t>(idx) >= playlist.size()) return;
        playlist.SetCurrentIndex(idx);
        engine.Open(playlist.at(static_cast<size_t>(idx)));
        notifyNowPlayingChanged(true);
        applyPerSongEqForCurrentTrack();
    };

    // The Eject menu's two items (below) need to add files/a folder to
    // the playlist the same way a drag-and-drop does, but that logic
    // (handleDroppedFile) isn't declared until after the playlist window's
    // state exists, further down this function. Deferring through
    // std::functions set later - rather than reordering a few hundred
    // lines of playlist setup above handleTransportPress - keeps this
    // change local to just the Eject feature.
    std::function<void()> onAddFilesRequested = [] {};
    std::function<void()> onAddFolderRequested = [] {};
    // In-app dropdown, replacing an earlier native "Files or Folder?"
    // AppleScript prompt (too jarring next to the rest of this UI) -
    // mirrors the Playlist window's Save-button menu pattern
    // (plSaveMenuOpen) one level up, in the main window instead.
    bool ejectMenuOpen = false;
#ifndef __APPLE__
    // Linux has no native menu bar (macOS gets its View/Effects menus from
    // app/main_menu.h's NSMenu bridging - see the #ifdef __APPLE__ blocks
    // below) - these back the in-window dropdown twins instead, same
    // mutual-exclusion convention as ejectMenuOpen/plSaveMenuOpen.
    bool viewMenuOpen = false;
    bool effectsMenuOpen = false;
#endif

    // Distinguishes "user pressed Stop" from "track finished naturally" -
    // both end up as engine.state() == Stopped, but only the latter should
    // auto-advance the playlist (see the Playing->Stopped edge check in the
    // main loop below, and PlayDone in the original's FunzioniGlobali.bas,
    // which this mirrors).
    bool userStoppedTrack = false;

    auto handleTransportPress = [&](TransportAction action) {
        switch (action) {
            case TransportAction::Play:
                if (engine.state() == audio::PlayState::Paused) {
                    // Resume.
                    engine.Play();
                    notifyNowPlayingChanged(true);
                } else if (engine.channels() != 0) {
                    // Mirrors the original: pressing Play while already
                    // playing restarts the current track from the
                    // beginning (`Call PlayStream(...)` fires
                    // unconditionally in the non-paused branch).
                    engine.SeekSeconds(0.0);
                    engine.Play();
                    notifyNowPlayingChanged(true);
                } else if (!playlist.empty()) {
                    // Nothing open yet: start from the armed index (or 0).
                    // openPlaylistIndex notifies on its own.
                    openPlaylistIndex(playlist.currentIndex() >= 0 ? playlist.currentIndex() : 0);
                }
                break;
            case TransportAction::Stop:
                userStoppedTrack = true;
                engine.Stop();
                notifyNowPlayingChanged(false);
                break;
            case TransportAction::Pause:
                // Original: mStreamIsActive ? mPauseStream : mResumeStream -
                // a toggle, unlike Play. No-op with nothing open.
                if (engine.channels() == 0) break;
                if (engine.state() == audio::PlayState::Playing) {
                    engine.Pause();
                    notifyNowPlayingChanged(false);
                } else {
                    engine.Play();
                    notifyNowPlayingChanged(true);
                }
                break;
            case TransportAction::Back:
            case TransportAction::Next: {
                // Mirrors MoveInMp3: always moves the armed index (with
                // wraparound), but only actually opens/plays the new track
                // if a stream is currently active - otherwise it just
                // moves the "next to play" pointer.
                if (playlist.empty()) break;
                const int newIdx = playlist.WrappedIndex(action == TransportAction::Back ? -1 : 1);
                if (engine.state() == audio::PlayState::Playing) {
                    openPlaylistIndex(newIdx);
                } else {
                    playlist.SetCurrentIndex(newIdx);
                }
                break;
            }
            case TransportAction::Eject:
                ejectMenuOpen = !ejectMenuOpen;
#ifndef __APPLE__
                viewMenuOpen = false;
                effectsMenuOpen = false;
#endif
                break;
        }
    };

    // Mirrors modFunzioniAccessorie.bas's "DurataStream(...) & ' - ' &
    // name" row format. Duration isn't available from Playlist itself (it
    // only stores paths), and querying it means opening a decoder just for
    // metadata - cheap for FLAC (duration is in the STREAMINFO header) but
    // a real full-file scan for MP3 (dr_mp3 has no frame-count header to
    // read - see Mp3Decoder::totalFrames' comment), so this is cached per
    // path and only ever probed for rows that are actually needed - not
    // the whole list up front. Declared this early (rather than down by
    // the rest of the playlist window's state) so both drawFrame (Main's
    // marquee) and drawPlaylistFrame (below) can share it.
    std::unordered_map<std::string, double> plDurationCache; // seconds, or -1.0 = unreadable
    auto probeDuration = [](const std::string& path) -> double {
        try {
            auto dec = audio::OpenDecoder(path);
            if (!dec || dec->sampleRate() == 0) return -1.0;
            return static_cast<double>(dec->totalFrames()) / dec->sampleRate();
        } catch (const std::exception&) {
            return -1.0;
        }
    };
    auto getPlaylistDuration = [&](size_t idx) -> double {
        // The currently-open track's duration is already known for free
        // (and more precisely - decoder_test/engine already opened it) -
        // no need to open a second, throwaway decoder for it.
        if (engine.channels() != 0 && playlist.currentIndex() >= 0 &&
            idx == static_cast<size_t>(playlist.currentIndex())) {
            return engine.durationSeconds();
        }
        const std::string& path = playlist.at(idx);
        auto it = plDurationCache.find(path);
        if (it != plDurationCache.end()) return it->second;
        const double d = probeDuration(path);
        plDurationCache[path] = d;
        return d;
    };
    auto formatDuration = [](double seconds) -> std::string {
        if (seconds < 0.0) return "--:--";
        const int total = static_cast<int>(seconds);
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%02d:%02d", total / 60, total % 60);
        return buf;
    };

    // TODO: "PL has the name of the song if present, otherwise the
    // filename" and (this session) "same for the main window". Same
    // caching shape as duration above (a real per-file read - ID3v2/
    // ID3v1 for MP3, VORBIS_COMMENT for FLAC - so it's cached per path
    // and only probed for tracks actually displayed). "" (no tag, or
    // unreadable) falls back to the bare filename. Shared by both windows
    // for the same reason getPlaylistDuration is declared up here.
    std::unordered_map<std::string, std::string> plTitleCache;
    auto getPlaylistDisplayName = [&](size_t idx) -> std::string {
        const std::string& path = playlist.at(idx);
        auto it = plTitleCache.find(path);
        std::string title;
        if (it != plTitleCache.end()) {
            title = it->second;
        } else {
            title = audio::ReadTrackTitle(path);
            plTitleCache[path] = title;
        }
        return title.empty() ? BaseName(path) : title;
    };

#ifdef __APPLE__
    notifyNowPlayingChanged = [&](bool isPlaying) {
        if (playlist.empty()) return;
        // Falls back to the first track when nothing's armed yet (mirrors
        // TransportAction::Play's own "nothing open yet: start from the
        // armed index, or 0" fallback) - without this, a playlist that's
        // been loaded/dropped-onto but never actually played leaves
        // currentIndex() at -1 forever, so this claim never fires and a
        // first Bluetooth press falls through to macOS's default handler
        // (Apple Music) instead of reaching this app at all, even though
        // there's a full queue sitting right here ready to go.
        const size_t idx =
            playlist.currentIndex() >= 0 ? static_cast<size_t>(playlist.currentIndex()) : 0;
        app::UpdateNowPlayingInfo(getPlaylistDisplayName(idx), engine.durationSeconds(), engine.positionSeconds(),
                                   isPlaying);
    };
    // The startup auto-open above (resumeSession's wasPlaying branch, and
    // the plain positional-tracks branch) calls engine.Open() directly
    // rather than through openPlaylistIndex, and runs before this lambda
    // even has its real body assigned - so a track that starts playing
    // right at launch never got a Now Playing claim in for it at all
    // until the periodic refresh caught up seconds later. Catches up
    // immediately instead - unconditionally on the playlist being
    // non-empty, not just when something's already playing, so a loaded
    // but not-yet-played queue also claims a provisional (paused) Now
    // Playing status right away rather than waiting on the periodic
    // refresh below.
    notifyNowPlayingChanged(engine.state() == audio::PlayState::Playing);
#endif

    CachedTextTexture marqueeTextCache, durationTextCache, freqTextCache, volTextCache, bitRateTextCache;
    CachedTextTexture peakLLabelCache, peakRLabelCache; // "L"/"R" next to the peak meter row (TODO)
    CachedTextTexture plToggleTextCache, eqToggleTextCache, visTooltipTextCache;
    std::array<CachedTextTexture, 2> ejectMenuTextCache; // Add Files, Add Folder
#ifndef __APPLE__
    CachedTextTexture viewToggleTextCache, effectsToggleTextCache;
    std::array<CachedTextTexture, kViewMenuItems> viewMenuTextCache;
    std::array<CachedTextTexture, kEffectsMenuItems> effectsMenuTextCache;
#endif

    auto drawFrame = [&]() {
        SDL_SetRenderDrawColor(renderer, 0x10, 0x12, 0x09, 255);
        SDL_RenderClear(renderer);

        Bevel::Draw(renderer, 0, 0, kWindowW, kWindowH);

        DrawTextureAt(renderer, texLogo, kLogoX, kLogoY);
        DrawTextureAt(renderer, texLite, kLiteX, kLiteY);
        DrawTextureAt(renderer, texShock, kShockX, kShockY);
        DrawTextureAt(renderer, texMinimize, kMinimizeX, kMinimizeY);
        DrawCloseIcon(renderer, kCloseX, kCloseY, kCloseSize);

        // New: Winamp-style PL/EQ toggle buttons (see layout.h - not in
        // the original, which had no way to reopen these from xmp itself).
        {
            // plUserVisible/eqUserVisible (the user's intent), not the raw
            // SDL_WINDOW_HIDDEN flag - that flag also reads true while a
            // window is merely minimized as part of the cascade below, so
            // using it here would dim these buttons during a minimize
            // even though the user never asked to hide either window.
            auto drawToggle = [&](int bx, int by, int bw, int bh, const char* label, bool active,
                                   CachedTextTexture& labelCache) {
                // TODO: "the PL and EQ buttons are ugly ... uniform ...
                // green instead of black where the letters are" - fill was
                // previously near-black (0x14-0x15 range) in both states,
                // reading as a black box with a green outline; now a
                // genuinely green fill throughout (brighter still when
                // active), with the text-transparency fix above (see
                // BlitCell) letting the label sit directly on it instead
                // of punching its own black rectangle through the middle.
                SDL_SetRenderDrawColor(renderer, active ? 0x2a : 0x1a, active ? 0x8f : 0x5a, active ? 0x46 : 0x2a,
                                        255);
                SDL_Rect bg{bx, by, bw, bh};
                SDL_RenderFillRect(renderer, &bg);
                SDL_SetRenderDrawColor(renderer, active ? 0x3d : 0x2a, active ? 0xff : 0x7a, active ? 0x74 : 0x3a,
                                        255);
                SDL_RenderDrawRect(renderer, &bg);
                DrawTextureAt(renderer, labelCache.Get(renderer, font, label, 2 * gfx::BitmapFont::kCellW),
                              bx + (bw - 2 * gfx::BitmapFont::kCellW) / 2, by + (bh - gfx::BitmapFont::kCellH) / 2);
            };
            drawToggle(kPlToggleX, kPlToggleY, kPlToggleW, kPlToggleH, "PL", plUserVisible, plToggleTextCache);
            drawToggle(kEqToggleX, kEqToggleY, kEqToggleW, kEqToggleH, "EQ", eqUserVisible, eqToggleTextCache);
#ifndef __APPLE__
            // View/Effects dropdown triggers - macOS gets real NSMenu items
            // instead (app/main_menu.h), so these only exist here.
            drawToggle(kViewToggleX, kViewToggleY, kViewToggleW, kViewToggleH, "VW", viewMenuOpen,
                       viewToggleTextCache);
            drawToggle(kEffectsToggleX, kEffectsToggleY, kEffectsToggleW, kEffectsToggleH, "FX", effectsMenuOpen,
                       effectsToggleTextCache);
#endif
        }

        // lnSposta/lnSposta2: the green double-line "handle" strip under the
        // title area, visible in a real screenshot of the original.
        SDL_SetRenderDrawColor(renderer, 0x1a, 0x66, 0x2e, 255);
        SDL_RenderDrawLine(renderer, 40, 13, 288, 13);
        SDL_RenderDrawLine(renderer, 40, 16, 288, 16);

        const bool trackOpen = engine.channels() != 0;
        {
            // Mirrors Clear_Click's xCaption = "Nope" when the playlist is
            // empty; otherwise shows the currently-open file's tagged
            // title if present, else its filename (getPlaylistDisplayName
            // - same TODO, same logic as the Playlist window's rows), or
            // the startup banner if nothing's been opened yet this
            // session. No asterisks here (see texStatus above) and no
            // border box - two real screenshots both show this as plain
            // floating text. The version in that banner is this revival's
            // own (app/version.h), not the original's "v1.0.378" this
            // used to hardcode - texStatus's version number was switched
            // the same way (see its own comment for why an initial pass
            // left it alone, and why that was reversed).
            std::string marqueeText = std::string("X-MAD.PLAYER v") + app::kVersion;
            if (trackOpen && playlist.currentIndex() >= 0) {
                marqueeText = getPlaylistDisplayName(static_cast<size_t>(playlist.currentIndex()));
            } else if (playlist.empty()) {
                marqueeText = "Nope";
            }
            DrawTextureAt(renderer, marqueeTextCache.Get(renderer, font, marqueeText, kDisplayFieldW), kMarqueeX,
                          kMarqueeY);
        }
        DrawTextureAt(renderer, texStatus, kStatusX, kStatusY);

        // seek track - thumb position reflects real playback progress once
        // a track is open, otherwise a static demo position.
        SDL_SetRenderDrawColor(renderer, 0x15, 0x52, 0x26, 255);
        SDL_RenderDrawLine(renderer, kSeekX0, kSeekY, kSeekX1, kSeekY);
        SDL_SetRenderDrawColor(renderer, 0x3d, 0xff, 0x74, 255);
        float seekFrac = 0.2f;
        if (trackOpen && engine.durationSeconds() > 0.0) {
            seekFrac = static_cast<float>(engine.positionSeconds() / engine.durationSeconds());
        }
        const int seekTravel = (kSeekX1 - kSeekX0) - 8;
        SDL_Rect thumb{kSeekX0 + static_cast<int>(std::clamp(seekFrac, 0.0f, 1.0f) * seekTravel), kSeekY - 4, 8,
                       8};
        SDL_RenderFillRect(renderer, &thumb);

        // transport row: Back, Play, Stop, Next, Pause, Eject (runtime order)
        int tx = kTransportX0;
        for (SDL_Texture* t : {texBack, texPlay, texStop, texNext, texPause, texEject}) {
            DrawTextureAt(renderer, t, tx, kTransportY);
            tx += kTransportBtnW;
        }
        // pressed-button highlight, matching IlluminaPulsante's green outline
        if (pressedButton >= 0) {
            SDL_SetRenderDrawColor(renderer, 0x3d, 0xff, 0x74, 255);
            SDL_Rect hi = transportButtons[pressedButton].rect;
            hi.x -= 1;
            hi.y -= 1;
            hi.w += 2;
            hi.h += 2;
            SDL_RenderDrawRect(renderer, &hi);
        }

        DrawTextureAt(renderer, texVol, kShowVolX, kShowVolY);
        DrawTextureAt(renderer, texMute, kMuteX, kMuteY);
        DrawTextureAt(renderer, texSpecMode, kSpecModeX, kSpecModeY);
        DrawTextureAt(renderer, texInfo, kInfoX, kInfoY);
        if (pressedUtility >= 0) {
            SDL_SetRenderDrawColor(renderer, 0x3d, 0xff, 0x74, 255);
            SDL_Rect hi = utilityButtons[static_cast<size_t>(pressedUtility)].rect;
            hi.x -= 1;
            hi.y -= 1;
            hi.w += 2;
            hi.h += 2;
            SDL_RenderDrawRect(renderer, &hi);
        }
        // Persistent Mute outline while active, matching IlluminaPulsante's
        // LockButton behavior (stays lit until pressed again, not just for
        // the duration of the click).
        if (engine.IsMuted()) {
            SDL_SetRenderDrawColor(renderer, 0x3d, 0xff, 0x74, 255);
            SDL_Rect hi{kMuteX - 1, kMuteY - 1, kTransportBtnW + 2, kTransportBtnH + 2};
            SDL_RenderDrawRect(renderer, &hi);
        }

        // Readout panel: a real screenshot of the original shows Time/Mode/
        // BitRate/Freq grouped in one bevelled box, not floating loose text.
        DrawInsetPanel(renderer, kReadoutPanelX, kReadoutPanelY, kReadoutPanelW, kReadoutPanelH);

        // LCD readouts. Duration and Freq are rebuilt each frame from live
        // engine state (cheap: these are tiny ~30x10px canvases); the rest
        // stay static placeholders (see comment above their creation).
        {
            char buf[16];
            const int posSec = trackOpen ? static_cast<int>(engine.positionSeconds()) : 0;
            std::snprintf(buf, sizeof(buf), "%02d:%02d", posSec / 60, posSec % 60);
            DrawTextureAt(renderer, durationTextCache.Get(renderer, font, buf, kDurationW), kDurationX, kDurationY);
        }
        {
            char buf[16] = "44kHz";
            if (trackOpen) std::snprintf(buf, sizeof(buf), "%ukHz", engine.sampleRate() / 1000);
            DrawTextureAt(renderer, freqTextCache.Get(renderer, font, buf, kFreqW), kFreqX, kFreqY);
        }
        {
            // Mirrors SettaIndicatoreModo's precedence: XSound (if the
            // toggle is on) overrides the actual channel-count-derived
            // Stereo/Mono state; no track loaded shows MODO0 (Azzerato) -
            // the original always assigns picModo *some* picture, never
            // leaves it blank.
            SDL_Texture* modeTex = texModeNone;
            if (engine.XSound()) modeTex = texModeXSound;
            else if (trackOpen) modeTex = engine.channels() == 1 ? texModeMono : texModeStereo;
            DrawTextureAt(renderer, modeTex, kModeIconX, kModeIconY);
        }
        DrawTextureAt(renderer, texModeLabel, kModeLabelX, kModeLabelY);
        {
            // Muted displays as 0, matching the original's slider-parked-at-
            // zero visual (see SetMuted's doc comment) rather than showing
            // the still-remembered volume_ value.
            const int volPct = static_cast<int>(std::lround((engine.IsMuted() ? 0.0f : engine.Volume()) * 100.0f));
            char buf[16];
            std::snprintf(buf, sizeof(buf), "%03d", std::clamp(volPct, 0, 100));
            DrawTextureAt(renderer, volTextCache.Get(renderer, font, buf, kVolTextW), kVolTextX, kVolTextY);
        }
        {
            // TODO: "the Khz and bit rate labels are fake" - this used to
            // be a permanently-hardcoded "128" texture, drawn regardless
            // of what was actually open. Now the same average-bitrate
            // calculation the Info window already used for real (see
            // ComputeAvgBitrateKbps), just also surfaced here.
            char buf[16] = "128k";
            if (trackOpen && playlist.currentIndex() >= 0) {
                const int kbps = ComputeAvgBitrateKbps(playlist.at(static_cast<size_t>(playlist.currentIndex())),
                                                         engine.durationSeconds());
                if (kbps > 0) std::snprintf(buf, sizeof(buf), "%dk", kbps);
            }
            DrawTextureAt(renderer, bitRateTextCache.Get(renderer, font, buf, kBitRateW), kBitRateX, kBitRateY);
        }

        // xmSlide (volume): up arrow, thumb between two guide lines, down arrow
        DrawTextureAt(renderer, texVolUp, kVolSliderX + (kVolSliderW - kVolArrowSize) / 2, kVolSliderY);
        DrawTextureAt(renderer, texVolDown,
                      kVolSliderX + (kVolSliderW - kVolArrowSize) / 2,
                      kVolSliderY + kVolSliderH - kVolArrowSize);
        {
            const int trackTop = kVolSliderY + kVolArrowSize;
            const int trackBottom = kVolSliderY + kVolSliderH - kVolArrowSize;
            const int trackH = trackBottom - trackTop;
            const float displayVolume = engine.IsMuted() ? 0.0f : engine.Volume();
            const int thumbH = 6;
            const int thumbY = trackTop + static_cast<int>((1.0f - displayVolume) * (trackH - thumbH));

            SDL_SetRenderDrawColor(renderer, 0xce, 0xce, 0xce, 255);
            const int lineX1 = kVolSliderX + kVolSliderW / 2 - 3;
            const int lineX2 = kVolSliderX + kVolSliderW / 2 + 3;
            SDL_RenderDrawLine(renderer, lineX1, trackTop, lineX1, trackBottom);
            SDL_RenderDrawLine(renderer, lineX2, trackTop, lineX2, trackBottom);

            SDL_SetRenderDrawColor(renderer, 0x00, 0xff, 0x00, 255); // vbGreen, picSlide.BackColor
            SDL_Rect thumbRect{kVolSliderX + 1, thumbY, kVolSliderW - 2, thumbH};
            SDL_RenderFillRect(renderer, &thumbRect);
        }

        // Dashed vertical divider between the analyzer and the readout/
        // transport zone - visible in a real screenshot of the original.
        DrawDashedVLine(renderer, kAnalyzerDividerX, kAnalyzerY, kAnalyzerY + kAnalyzerH, 0x5a, 0x60, 0x50);

        // Real spectrum + peak data, captured from the audio callback.
        // Fetched once and shared by both the bar analyzer and the peak
        // meters below. Bar geometry (Gap=2, Barwidth=1) matches
        // BitBltSpec's XX = (I + 2/Gap) * Gap indexing from volume.bas.
        {
            std::vector<float> snap;
            int snapChannels = 0;
            const bool haveSnap = trackOpen && engine.state() == audio::PlayState::Playing &&
                                   engine.GetVisSnapshot(snap, snapChannels) && snapChannels > 0 &&
                                   static_cast<int>(snap.size()) >= kFftPoints * snapChannels;

            if (haveSnap) {
                // Peak meters (L/R) always track real levels regardless of
                // visMode - in the original these are a separate display
                // (gpeak/gph/abuff) from the analyzer that SpecMode toggles.
                float l = 0.0f, r = 0.0f;
                for (int i = 0; i < kFftPoints; ++i) {
                    l = std::max(l, std::abs(snap[static_cast<size_t>(i) * snapChannels]));
                    r = std::max(r, std::abs(snap[static_cast<size_t>(i) * snapChannels + (snapChannels > 1 ? 1 : 0)]));
                }
                peakL = l;
                peakR = r;
            } else {
                peakL = std::max(0.0f, peakL - 0.05f);
                peakR = std::max(0.0f, peakR - 0.05f);
            }

            // VisPanel::Analyzer's own 6 SpecMode sub-modes - only drawn
            // while the analyzer itself is the panel showing; IdleLogo/
            // CardioOSC (below) share this same box for the other two
            // states in the 3-way click cycle.
            if (visPanel == VisPanel::Analyzer) {
            const bool barMode = visMode == VisMode::PeakFalls || visMode == VisMode::NoPeakFalls ||
                                  visMode == VisMode::PeakNoFalls || visMode == VisMode::FadeFft;
            if (barMode) {
                if (haveSnap) {
                    for (int i = 0; i < kFftPoints; ++i) {
                        float s = 0.0f;
                        for (int c = 0; c < snapChannels; ++c) s += snap[static_cast<size_t>(i) * snapChannels + c];
                        spectrumAnalyzer.Feed(i, s / snapChannels);
                    }
                    std::vector<int> spec(kFftPoints / 2);
                    spectrumAnalyzer.Transform(spec.data());
                    for (int bar = 0; bar < kNumBars; ++bar) {
                        int peakDb = -180;
                        for (int bin = barBinEdges[static_cast<size_t>(bar)];
                             bin < barBinEdges[static_cast<size_t>(bar + 1)]; ++bin) {
                            peakDb = std::max(peakDb, spec[static_cast<size_t>(bin)]);
                        }
                        // -60..0 dB -> 0..1 (an arbitrary but reasonable visual range).
                        barLevels[static_cast<size_t>(bar)] = std::clamp((peakDb + 60.0f) / 60.0f, 0.0f, 1.0f);
                    }
                } else {
                    // No fresh data (paused/stopped/no track): fall to silence.
                    for (auto& v : barLevels) v = std::max(0.0f, v - 0.05f);
                }

                // User feedback, in order: first FadeFft ("the bars need
                // to be larger, enough to fill the difference with OSC"),
                // then the same for the three remaining bar modes
                // ("Stone Peak Falls"/"Stone Falls"/"Stone Peak" -
                // PeakFalls/NoPeakFalls/PeakNoFalls), then "I want to see
                // a small gap between the bars with those though" for
                // just those three, then "the bars are of different
                // size, make them all the same size ... okay to leave
                // some black pixels" - the previous evenly-distributed
                // segment widths varied by 1px (e.g. 3 or 4) depending on
                // rounding, which is what read as inconsistent. Every bar
                // is now a fixed kBarSegW px wide (floor(kAnalyzerW/
                // kNumBars) - kAnalyzerW isn't evenly divisible by
                // kNumBars, so this always leaves a remainder) rather than
                // distributing it invisibly across the bars as slightly-
                // different widths. That remainder was originally pushed
                // to the left edge (row right-aligned within the analyzer
                // box); a later request flipped it to the right instead
                // (`kBarRowX0` now just left-aligns at kAnalyzerX, so the
                // leftover naturally falls after the last bar). FadeFft
                // stays fully gapless (matches the real `DisgnaxmMP3FFT`
                // renderer's true per-pixel fill); the other three get a
                // deliberate 1px gap carved out of each otherwise-fixed-
                // width bar.
                constexpr int kBarGapPx = 1;
                constexpr int kBarSegW = kAnalyzerW / kNumBars;
                constexpr int kBarRowX0 = kAnalyzerX;
                for (int i = 0; i < kNumBars; ++i) {
                    const int bx = kBarRowX0 + i * kBarSegW;
                    const int gap = visMode == VisMode::FadeFft ? 0 : kBarGapPx;
                    const int barW = std::max(1, kBarSegW - gap);
                    if (bx >= kAnalyzerX + kAnalyzerW) break;
                    const float targetPx = barLevels[static_cast<size_t>(i)] * kAnalyzerH;

                    if (visMode == VisMode::FadeFft) {
                        // Fast-reactive fill: instant rise, quick decay.
                        if (targetPx > fadeFillPx[static_cast<size_t>(i)]) {
                            fadeFillPx[static_cast<size_t>(i)] = targetPx;
                        } else {
                            fadeFillPx[static_cast<size_t>(i)] =
                                std::max(0.0f, fadeFillPx[static_cast<size_t>(i)] - kFadeFillDecayPx);
                        }
                        // Slower trailing peak line, riding above the fill.
                        if (fadeFillPx[static_cast<size_t>(i)] > fadeTrailPx[static_cast<size_t>(i)]) {
                            fadeTrailPx[static_cast<size_t>(i)] = fadeFillPx[static_cast<size_t>(i)];
                        } else {
                            fadeTrailPx[static_cast<size_t>(i)] =
                                std::max(0.0f, fadeTrailPx[static_cast<size_t>(i)] - kFadeTrailDecayPx);
                        }
                        DrawFadeColumn(renderer, bx, kAnalyzerY, barW, kAnalyzerH, fadeFillPx[static_cast<size_t>(i)]);
                        DrawPeakMarker(renderer, bx, kAnalyzerY, barW, kAnalyzerH, fadeTrailPx[static_cast<size_t>(i)],
                                       0x3d, 0xff, 0x74);
                    } else {
                        // PeakFalls / NoPeakFalls / PeakNoFalls: BitBltSpec's
                        // ballistics - instant rise, eased fall; PeakNoFalls
                        // falls at the faster rate (noFallsVel).
                        const float fallSpeed = (visMode == VisMode::PeakNoFalls) ? kFallsVelFast : kFallsVelSlow;
                        if (targetPx > barHeightPx[static_cast<size_t>(i)]) {
                            barHeightPx[static_cast<size_t>(i)] = targetPx;
                        } else {
                            barHeightPx[static_cast<size_t>(i)] =
                                std::max(targetPx, barHeightPx[static_cast<size_t>(i)] - fallSpeed);
                        }
                        DrawGradientBar(renderer, bx, kAnalyzerY, barW, kAnalyzerH,
                                        barHeightPx[static_cast<size_t>(i)] / kAnalyzerH);

                        const bool showPeak = visMode != VisMode::NoPeakFalls;
                        if (showPeak) {
                            if (barHeightPx[static_cast<size_t>(i)] >= peakHeightPx[static_cast<size_t>(i)]) {
                                peakHeightPx[static_cast<size_t>(i)] =
                                    std::min(static_cast<float>(kAnalyzerH),
                                             barHeightPx[static_cast<size_t>(i)] + kPeakOffsetPx);
                            } else {
                                peakHeightPx[static_cast<size_t>(i)] =
                                    std::max(barHeightPx[static_cast<size_t>(i)],
                                             peakHeightPx[static_cast<size_t>(i)] - kPeakCreepPx);
                            }
                            // Blue, not green - matches a real screenshot of the original.
                            DrawPeakMarker(renderer, bx, kAnalyzerY, barW, kAnalyzerH,
                                           peakHeightPx[static_cast<size_t>(i)], 0x4a, 0x9c, 0xf0);
                        }
                    }
                }
            } else {
                // Oscilloscope / StereoOscilloscope - mirrors ShowMovingLine
                // / ShowMovingLineStereo's connected-line-segment waveform,
                // fed from the same real vis snapshot (not xmMP3_getWave,
                // which doesn't exist here - same idea, new source). Both
                // channels are plain vbGreen in the original - the only
                // separation is that the right channel's X origin gets a
                // `+1` pixel offset (`XX = (ScaleWidth/2) + 1`), i.e. a
                // 1px gap, not a drawn divider or a color change.
                SDL_SetRenderDrawColor(renderer, 0x3d, 0xff, 0x74, 255);
                const int midY = kAnalyzerY + kAnalyzerH / 2;
                if (haveSnap) {
                    const bool stereo = visMode == VisMode::StereoOscilloscope && snapChannels > 1;
                    constexpr int kChannelGapPx = 1; // the original's "+1"
                    const int chanW = stereo ? (kAnalyzerW - kChannelGapPx) / 2 : kAnalyzerW;
                    // TODO: "the oscilloscope needs to be boosted in
                    // amplitude a little, so it's more visible". Typical
                    // mastered audio (plus this port's own default 25%
                    // volume - see Engine's volume_ default) rarely swings
                    // anywhere near +-1.0, so the raw sample value alone
                    // traces a barely-there line. A flat gain, clamped to
                    // the analyzer's own bounds so genuinely loud/high-
                    // volume material still clips flat rather than
                    // drawing outside the display, rather than a deeper
                    // rework of where the snapshot is captured (pre- vs
                    // post-volume) - out of scope for "a little".
                    constexpr float kOscilloscopeBoost = 2.0f;
                    auto drawWave = [&](int originX, int width, const auto& sampleAt) {
                        int prevX = originX, prevY = midY;
                        for (int px = 0; px < width; ++px) {
                            const int sampleIdx =
                                std::min(kFftPoints - 1, px * kFftPoints / std::max(1, width));
                            const float s = sampleAt(sampleIdx);
                            const int y = std::clamp(midY - static_cast<int>(s * kOscilloscopeBoost * (kAnalyzerH / 2)),
                                                      kAnalyzerY, kAnalyzerY + kAnalyzerH - 1);
                            const int x = originX + px;
                            SDL_RenderDrawLine(renderer, prevX, prevY, x, y);
                            prevX = x;
                            prevY = y;
                        }
                    };
                    auto channelSample = [&](int channelIndex) {
                        return [&, channelIndex](int idx) {
                            return snap[static_cast<size_t>(idx) * snapChannels + static_cast<size_t>(channelIndex)];
                        };
                    };
                    if (stereo) {
                        drawWave(kAnalyzerX, chanW, channelSample(0));
                        drawWave(kAnalyzerX + chanW + kChannelGapPx, chanW, channelSample(1));
                    } else {
                        // Mono Osc mirrors ShowMovingLine's actual mix -
                        // (waveTblL(I) + waveTblR(I)) / 2 - not just the
                        // left channel alone. A single-channel drawChannel
                        // call here silently dropped the right channel
                        // entirely for any stereo file with L != R content
                        // (e.g. anything panned, or this port's own XSound
                        // stereo-widen effect) - a real divergence caught
                        // while re-auditing every mode against the
                        // original source, not a style choice.
                        drawWave(kAnalyzerX, chanW, [&](int idx) {
                            if (snapChannels > 1) {
                                return (snap[static_cast<size_t>(idx) * snapChannels] +
                                        snap[static_cast<size_t>(idx) * snapChannels + 1]) /
                                       2.0f;
                            }
                            return snap[static_cast<size_t>(idx) * snapChannels];
                        });
                    }
                } else {
                    SDL_RenderDrawLine(renderer, kAnalyzerX, midY, kAnalyzerX + kAnalyzerW, midY);
                }
            }
            } else if (visPanel == VisPanel::IdleLogo) {
                // The static skull/"XmP" branding image (ImgLogo,
                // "CPULESS" in the original's config). Stretched
                // (ImgLogo.Stretch = True) but NOT into the analyzer's own
                // box - the original's ImgLogo control is a separate,
                // taller box (Height=600 twips=40px vs. analyzer's own
                // 23px) - squashing the logo's real ~100x45 aspect ratio
                // into the analyzer's flatter 89x23 box visibly distorted
                // it (user-reported). Instead: fill the analyzer's WIDTH
                // (keeps the same click hit-box, same horizontal footprint
                // as the other two panels) and derive height from the
                // logo's own native aspect ratio - 89 * 45/100 = ~40px,
                // matching the original's real 40px design value almost
                // exactly - then vertically center that on the analyzer's
                // own center, letting it extend slightly above/below like
                // the original's taller box does (verified there's room:
                // nothing else occupies x=[kAnalyzerX,kAnalyzerX+
                // kAnalyzerW) between the volume slider above and the
                // transport row below).
                const int logoH = kAnalyzerW * skin.idleLogo.height / skin.idleLogo.width;
                const int logoY = kAnalyzerY + (kAnalyzerH - logoH) / 2;
                DrawTextureFit(renderer, texIdleLogo, kAnalyzerX, logoY, kAnalyzerW, logoH);
            } else {
                // VisPanel::CardioOSC: two VU bars (SpectrumSin/Des -
                // same PICGPH gradient the L/R peak meter row below
                // already reuses) plus two scrolling oscilloscope traces
                // (picCardioSin/Des - DisegnaCardioOSC's PSet scroll).
                const int sinVuX = kAnalyzerX;
                const int sinTraceX = sinVuX + kCardioVuW + kCardioGapPx;
                const int desVuX = kAnalyzerX + kCardioHalfW;
                const int desTraceX = desVuX + kCardioVuW + kCardioGapPx;

                DrawGradientBar(renderer, sinVuX, kAnalyzerY, kCardioVuW, kAnalyzerH, peakL);
                DrawGradientBar(renderer, desVuX, kAnalyzerY, kCardioVuW, kAnalyzerH, peakR);

                if (haveSnap) {
                    plotCardioColumn(cardioSinBuf, peakL);
                    plotCardioColumn(cardioDesBuf, peakR);
                    ++cardioScrollIdx;
                    if (cardioScrollIdx >= kCardioTraceW) {
                        cardioScrollIdx = 0;
                        std::fill(cardioSinBuf.rgba.begin(), cardioSinBuf.rgba.end(), 0);
                        std::fill(cardioDesBuf.rgba.begin(), cardioDesBuf.rgba.end(), 0);
                    }
                }
                SDL_Texture* tSin = UploadTexture(renderer, cardioSinBuf);
                SDL_Texture* tDes = UploadTexture(renderer, cardioDesBuf);
                DrawTextureAt(renderer, tSin, sinTraceX, kAnalyzerY);
                DrawTextureAt(renderer, tDes, desTraceX, kAnalyzerY);
                SDL_DestroyTexture(tSin);
                SDL_DestroyTexture(tDes);
            }
        }

        // L/R peak meter row (abuff). Lit-segment color follows
        // gfx::LevelGradientColor - the original's real green-to-yellow
        // PICGPH gradient (SpectrumSin/SpectrumDes), not a hand-guessed
        // 2-color split - see level_meter.h for how that gradient was
        // recovered and why a continuous per-segment sample is the right
        // way to reproduce a reveal-a-bitmap mechanic with discrete LEDs.
        // TODO: "L R in front of the two bars" - new, not in the original
        // (no ToolTipText/label was ever found for this row), added so
        // the two rows read as channels rather than an unlabeled pair.
        // Both the labels and kPeakX itself (layout.h) were later shifted
        // +8px right together (user follow-up) so the bars line up with
        // the analyzer box/transport row above - same 1px gap between
        // label and bar as originally, just both moved.
        {
            const std::array<float, 2> chLevel = {peakL, peakR};
            const int segCount = 20;
            const int segW = kPeakW / segCount - 1;
            constexpr int kPeakLabelX = kPeakX - gfx::BitmapFont::kCellW - 1;
            for (int ch = 0; ch < 2; ++ch) {
                const int litSegs = static_cast<int>(chLevel[static_cast<size_t>(ch)] * segCount);
                const int rowY = kPeakY + ch * (kPeakH / 2 + 2);
                const int rowH = (kPeakH / 2) - 2;
                CachedTextTexture& labelCache = ch == 0 ? peakLLabelCache : peakRLabelCache;
                const char* label = ch == 0 ? "L" : "R";
                DrawTextureAt(renderer, labelCache.Get(renderer, font, label, gfx::BitmapFont::kCellW), kPeakLabelX,
                              rowY + (rowH - gfx::BitmapFont::kCellH) / 2);
                for (int i = 0; i < segCount; ++i) {
                    const int sx = kPeakX + i * (segW + 1);
                    if (i < litSegs) {
                        const gfx::RGB c = gfx::LevelGradientColor(static_cast<float>(i) / (segCount - 1));
                        SDL_SetRenderDrawColor(renderer, c.r, c.g, c.b, 255);
                    } else {
                        SDL_SetRenderDrawColor(renderer, 0x15, 0x52, 0x26, 255);
                    }
                    SDL_Rect seg{sx, rowY, segW, (kPeakH / 2) - 2};
                    SDL_RenderFillRect(renderer, &seg);
                }
            }
        }

        // Visualization tooltip (user request: "put tooltips on the
        // visualizations so I can tell you which ones need refining") -
        // real ToolTipText values from the original (DisegaSpectrum sets
        // xmp.analyzer.ToolTipText per sub-mode; picCardioSin/Des's is a
        // fixed "CardioOSC" in xmp.frm) rather than invented labels, so
        // feedback can reference the actual mode by name. ImgLogo has no
        // ToolTipText in the original - "Idle Logo" here is this port's
        // own label, not a ported string. No native tooltip widget exists
        // in this app, so this is a small drawn box following the cursor,
        // shown for as long as it's over the analyzer's box (no hover
        // delay - immediate, simpler, and sufficient for its purpose).
        if (mainMouseX >= kAnalyzerX && mainMouseX < kAnalyzerX + kAnalyzerW && mainMouseY >= kAnalyzerY &&
            mainMouseY < kAnalyzerY + kAnalyzerH) {
            std::string label;
            if (visPanel == VisPanel::IdleLogo) {
                label = "Idle Logo";
            } else if (visPanel == VisPanel::CardioOSC) {
                label = "CardioOSC";
            } else {
                switch (visMode) {
                    case VisMode::PeakFalls: label = "xmMP3 Stone Peak Falls"; break;
                    case VisMode::NoPeakFalls: label = "xmMP3 Stone Falls"; break;
                    case VisMode::PeakNoFalls: label = "xmMP3 Stone Peak"; break;
                    case VisMode::FadeFft: label = "xmMP3 Fade FFT"; break;
                    case VisMode::Oscilloscope: label = "Osc"; break;
                    case VisMode::StereoOscilloscope: label = "Stereo Osc"; break;
                }
            }
            const int textW = static_cast<int>(label.size()) * gfx::BitmapFont::kCellW;
            const int boxW = textW + 6;
            const int boxH = gfx::BitmapFont::kCellH + 4;
            int bx = std::clamp(mainMouseX + 10, 2, kWindowW - 2 - boxW);
            int by = std::clamp(mainMouseY + 10, 2, kWindowH - 2 - boxH);
            SDL_SetRenderDrawColor(renderer, 0x05, 0x06, 0x03, 255);
            SDL_Rect bg{bx, by, boxW, boxH};
            SDL_RenderFillRect(renderer, &bg);
            SDL_SetRenderDrawColor(renderer, 0x3d, 0xff, 0x74, 255);
            SDL_RenderDrawRect(renderer, &bg);
            DrawTextureAt(renderer, visTooltipTextCache.Get(renderer, font, label, textW), bx + 3, by + 2);
        }

        if (ejectMenuOpen) {
            // Same style as the Playlist window's Save menu (Bevel::Draw
            // fills an opaque background before bordering) - drawn last
            // so it sits on top of everything else in this window.
            Bevel::Draw(renderer, kEjectMenuX, kEjectMenuY, kEjectMenuW, kEjectMenuH);
            static const char* kEjectMenuItems[2] = {"ADD FILES", "ADD FOLDER"};
            for (int i = 0; i < 2; ++i) {
                const int iy = kEjectMenuY + i * kEjectMenuItemH;
                // DrawText auto-centers within the field width it's given
                // (PaintChar's "(ScaleWidth - Dimension*Len)/2" formula) -
                // passing each label its own exact width (rather than a
                // shared fixed field) makes that offset 0 for both, so
                // "ADD FILES" (9 chars) and "ADD FOLDER" (10 chars) start
                // flush left at the same x instead of the shorter one
                // drifting right of the longer one.
                const int fieldWidth = static_cast<int>(std::strlen(kEjectMenuItems[i])) * gfx::BitmapFont::kCellW;
                DrawTextureAt(renderer,
                              ejectMenuTextCache[static_cast<size_t>(i)].Get(renderer, font, kEjectMenuItems[i],
                                                                              fieldWidth),
                              kEjectMenuX + 2, iy + (kEjectMenuItemH - gfx::BitmapFont::kCellH) / 2);
            }
            SDL_SetRenderDrawColor(renderer, 0x23, 0x26, 0x20, 255);
            SDL_RenderDrawLine(renderer, kEjectMenuX + 1, kEjectMenuY + kEjectMenuItemH,
                                kEjectMenuX + kEjectMenuW - 2, kEjectMenuY + kEjectMenuItemH);
        }

#ifndef __APPLE__
        // Linux equivalents of macOS's native View/Effects NSMenu items
        // (app/main_menu.h) - same Bevel::Draw-then-rows chrome as the
        // Eject/Save menus above, drawn last so they sit on top. No cached
        // "checked" struct to keep in sync (unlike SetUiScaleMenuChecked/
        // SetEffectsMenuChecked on macOS): this redraws from live engine/
        // uiScalePercent state every frame, so the highlighted row is
        // always correct with nothing to invalidate.
        if (viewMenuOpen) {
            Bevel::Draw(renderer, kViewMenuX, kViewMenuY, kViewMenuW, kViewMenuH);
            // No '%' glyph in this bitmap font (gfx::BitmapFont::CharToCell
            // falls through to a placeholder cell for it) - digits-only
            // labels avoid that instead of rendering a stray dot.
            static const char* kViewMenuLabels[kViewMenuItems] = {"100",    "200",      "300",    "400",
                                                                    "ZOOM IN", "ZOOM OUT", "RESET"};
            static constexpr int kViewMenuPresets[4] = {100, 200, 300, 400};
            for (int i = 0; i < kViewMenuItems; ++i) {
                const int iy = kViewMenuY + i * kViewMenuItemH;
                const bool isCurrent = i < 4 && uiScalePercent == kViewMenuPresets[i];
                if (isCurrent) {
                    SDL_SetRenderDrawColor(renderer, 0x1a, 0x5a, 0x2a, 255);
                    SDL_Rect hi{kViewMenuX + 1, iy, kViewMenuW - 2, kViewMenuItemH};
                    SDL_RenderFillRect(renderer, &hi);
                }
                const int fieldWidth = static_cast<int>(std::strlen(kViewMenuLabels[i])) * gfx::BitmapFont::kCellW;
                DrawTextureAt(renderer,
                              viewMenuTextCache[static_cast<size_t>(i)].Get(renderer, font, kViewMenuLabels[i],
                                                                             fieldWidth),
                              kViewMenuX + 2, iy + (kViewMenuItemH - gfx::BitmapFont::kCellH) / 2);
            }
            SDL_SetRenderDrawColor(renderer, 0x23, 0x26, 0x20, 255);
            SDL_RenderDrawLine(renderer, kViewMenuX + 1, kViewMenuY + 4 * kViewMenuItemH,
                                kViewMenuX + kViewMenuW - 2, kViewMenuY + 4 * kViewMenuItemH);
        }

        if (effectsMenuOpen) {
            Bevel::Draw(renderer, kEffectsMenuX, kEffectsMenuY, kEffectsMenuW, kEffectsMenuH);
            static const char* kEffectsMenuLabels[kEffectsMenuItems] = {"XSOUND", "REVERB", "SATURATION",
                                                                          "COMPRESSION", "CHORUS"};
            const bool effectsOn[kEffectsMenuItems] = {engine.XSound(), engine.ReverbOn(), engine.SaturationOn(),
                                                        engine.CompressionOn(), engine.ChorusOn()};
            for (int i = 0; i < kEffectsMenuItems; ++i) {
                const int iy = kEffectsMenuY + i * kEffectsMenuItemH;
                if (effectsOn[i]) {
                    SDL_SetRenderDrawColor(renderer, 0x1a, 0x5a, 0x2a, 255);
                    SDL_Rect hi{kEffectsMenuX + 1, iy, kEffectsMenuW - 2, kEffectsMenuItemH};
                    SDL_RenderFillRect(renderer, &hi);
                }
                const int fieldWidth =
                    static_cast<int>(std::strlen(kEffectsMenuLabels[i])) * gfx::BitmapFont::kCellW;
                DrawTextureAt(renderer,
                              effectsMenuTextCache[static_cast<size_t>(i)].Get(renderer, font, kEffectsMenuLabels[i],
                                                                                fieldWidth),
                              kEffectsMenuX + 2, iy + (kEffectsMenuItemH - gfx::BitmapFont::kCellH) / 2);
            }
        }
#endif
    };

    // ---- playlist window (Listone.frm) ----
    SDL_Texture* plTexClear = UploadTexture(plRenderer, skin.plClear);
    SDL_Texture* plTexDelete = UploadTexture(plRenderer, skin.plDelete);
    SDL_Texture* plTexSave = UploadTexture(plRenderer, skin.plSave);
    SDL_Texture* plTexUp = UploadTexture(plRenderer, skin.volUp);   // FRECCIAUP, same asset as xmSlide
    SDL_Texture* plTexDown = UploadTexture(plRenderer, skin.volDown); // FRECCIADWN
    SDL_Texture* plTexSeek = UploadTexture(plRenderer, skin.plSeek);

    constexpr int kPlVisibleRows = kPlaylistListH / kPlaylistRowH;
    constexpr int kPlButtonX[6] = {kPlaylistClearX,  kPlaylistDeleteX, kPlaylistSaveX,
                                    kPlaylistUpX, kPlaylistDownX,  kPlaylistSeekX};

    int plSelected = -1;    // ListaMp3.ListIndex analogue
    int plScrollOffset = 0; // first visible row index
    int plPressedButton = -1;
    Uint32 lastClickTimeMs = 0;
    int lastClickedRow = -1;
    bool plSaveMenuOpen = false; // Quick Save/Save As popup (see the Save button below)
    // Quick Save's write target - starts at the old hardcoded default,
    // retargeted by a successful Save As. In-memory only, not persisted
    // across restarts (not asked for).
    std::string plQuickSavePath = "xmad_playlist.m3u";
    bool plClearConfirmOpen = false; // "Are you sure?" popup (see the Clear button below)

    auto ensureRowVisible = [&](int row) {
        if (row < plScrollOffset) plScrollOffset = row;
        if (row >= plScrollOffset + kPlVisibleRows) plScrollOffset = row - kPlVisibleRows + 1;
        plScrollOffset = std::max(0, plScrollOffset);
    };

    // Split out from the Save button's menu-item handling (below) so a
    // debug hook can drive "the user picked Quick Save / already chose
    // path X in Save As" without a live OS dialog - see
    // --playlist-quick-save/--playlist-save-as.
    auto quickSavePlaylist = [&]() { playlist.SaveM3U(plQuickSavePath); };
    auto saveAsToPath = [&](const std::string& path) {
        playlist.SaveM3U(path);
        plQuickSavePath = path;
    };

    // Split out from the Clear button's confirmation popup (below) for the
    // same reason as quickSavePlaylist/saveAsToPath above: --clear-reload-
    // test calls this directly to exercise the real Clear->Add->Play
    // engine sequence without going through (and being gated by) the
    // confirmation UI, which isn't what that regression test is about.
    auto clearPlaylist = [&]() {
        // Mirrors Clear_Click -> StopAll -> mStopStream: fully releases the
        // open track, not just Stop() (which deliberately leaves the
        // decoder loaded so Play can restart it - wrong here, since
        // there's nothing left in the list for a restarted track to
        // belong to).
        engine.Close();
        playlist.Clear();
        plSelected = -1;
    };

    // Mirrors Listone.frm's CommandImg_Click, all 6 buttons (an earlier
    // pass here only implemented indices 0/1/2/3 of the original's 0-5 -
    // Save and Seek were missing entirely, not just decorative).
    auto handlePlaylistButtonPress = [&](int idx) {
        switch (idx) {
            case 0: // Clear - now asks for confirmation first (TODO:
                    // "clear playlist pops a window up that asks 'Are you
                    // sure you?'" - previously cleared unconditionally).
                    // The actual clear happens in handlePlaylistClickAt
                    // once confirmed. Only one popup at a time.
                plSaveMenuOpen = false;
                plClearConfirmOpen = !plClearConfirmOpen;
                break;
            case 1: // Delete - mirrors ListaMp3_KeyPress("d")
                if (plSelected >= 0 && static_cast<size_t>(plSelected) < playlist.size()) {
                    if (playlist.currentIndex() == plSelected) engine.Close();
                    playlist.RemoveAt(static_cast<size_t>(plSelected));
                    if (plSelected >= static_cast<int>(playlist.size())) {
                        plSelected = static_cast<int>(playlist.size()) - 1;
                    }
                }
                break;
            case 2: // Save - now pops a Quick Save/Save As menu (mirrors the
                    // original's frmMenu.mnuPlayList popup - previously
                    // stubbed as an unconditional write, since there was no
                    // menu system here yet). The actual writes happen in
                    // handlePlaylistClickAt once a choice is made. Only one
                    // popup at a time.
                plClearConfirmOpen = false;
                plSaveMenuOpen = !plSaveMenuOpen;
                break;
            case 3: // Up - mirrors SpostaItem(Su)
                if (plSelected > 0) {
                    playlist.MoveUp(static_cast<size_t>(plSelected));
                    plSelected -= 1;
                    ensureRowVisible(plSelected);
                }
                break;
            case 4: // Down - mirrors SpostaItem(Giu)
                if (plSelected >= 0 && static_cast<size_t>(plSelected) + 1 < playlist.size()) {
                    playlist.MoveDown(static_cast<size_t>(plSelected));
                    plSelected += 1;
                    ensureRowVisible(plSelected);
                }
                break;
            case 5: // Seek - mirrors CommandImg_Click Case 5: scroll the
                    // list to the currently-playing track.
                if (playlist.currentIndex() >= 0) {
                    plSelected = playlist.currentIndex();
                    ensureRowVisible(plSelected);
                }
                break;
        }
    };

    CachedTextTexture plTitleTextCache;
    std::array<CachedTextTexture, kPlVisibleRows> plRowTextCache;
    CachedTextTexture plInfo0TextCache, plInfo1TextCache;
    std::array<CachedTextTexture, 2> plSaveMenuTextCache; // Quick Save, Save As...
    std::array<CachedTextTexture, 3> plClearConfirmTextCache; // label, YES, NO

    auto drawPlaylistFrame = [&]() {
        SDL_SetRenderDrawColor(plRenderer, 0x10, 0x12, 0x09, 255);
        SDL_RenderClear(plRenderer);
        Bevel::Draw(plRenderer, 0, 0, kPlaylistWindowW, kPlaylistWindowH);
        DrawWindowHeader(plRenderer, kPlaylistWindowW, kDragStripH);
        {
            DrawTextureAt(plRenderer, plTitleTextCache.Get(plRenderer, font, "PLAYLIST", 8 * gfx::BitmapFont::kCellW),
                          8, (kDragStripH - gfx::BitmapFont::kCellH) / 2);
        }
        DrawCloseIcon(plRenderer, kCloseX, kCloseY, kCloseSize);

        for (int row = 0; row < kPlVisibleRows; ++row) {
            const int idx = plScrollOffset + row;
            if (idx < 0 || static_cast<size_t>(idx) >= playlist.size()) break;
            const int ry = kPlaylistListY + row * kPlaylistRowH;
            const bool isPlaying = (idx == playlist.currentIndex());
            const bool isSelected = (idx == plSelected);

            if (isPlaying) {
                // Solid, dim fill for the now-playing row - a quarter-
                // brightness version of the UI's accent green, dark enough
                // that the full-brightness row text (drawn on top, below)
                // stays legible against it.
                SDL_SetRenderDrawColor(plRenderer, 0x0f, 0x40, 0x1d, 255);
                SDL_Rect bg{kPlaylistListX, ry, kPlaylistListW, kPlaylistRowH};
                SDL_RenderFillRect(plRenderer, &bg);
            }
            if (isSelected) {
                SDL_SetRenderDrawColor(plRenderer, 0x3d, 0xff, 0x74, 255);
                SDL_Rect bg{kPlaylistListX, ry, kPlaylistListW, kPlaylistRowH};
                SDL_RenderDrawRect(plRenderer, &bg);
            }

            const int maxChars = (kPlaylistListW - 4) / gfx::BitmapFont::kCellW;
            // Mirrors "DurataStream(...) & ' - ' & name" (see
            // getPlaylistDuration's doc comment above); name is the tagged
            // title if present, else the filename (getPlaylistDisplayName).
            std::string label = formatDuration(getPlaylistDuration(static_cast<size_t>(idx))) + " - " +
                                 getPlaylistDisplayName(static_cast<size_t>(idx));
            if (static_cast<int>(label.size()) > maxChars) label = label.substr(0, static_cast<size_t>(maxChars));
            DrawTextureAt(plRenderer,
                          plRowTextCache[static_cast<size_t>(row)].Get(
                              plRenderer, font, label, static_cast<int>(label.size()) * gfx::BitmapFont::kCellW),
                          kPlaylistListX + 2, ry + 1);
        }

        SDL_SetRenderDrawColor(plRenderer, 0x23, 0x26, 0x20, 255);
        SDL_Rect listBorder{kPlaylistListX, kPlaylistListY, kPlaylistListW, kPlaylistListH};
        SDL_RenderDrawRect(plRenderer, &listBorder);

        DrawTextureAt(plRenderer, plTexClear, kPlaylistClearX, kPlaylistBtnY);
        DrawTextureAt(plRenderer, plTexDelete, kPlaylistDeleteX, kPlaylistBtnY);
        DrawTextureAt(plRenderer, plTexSave, kPlaylistSaveX, kPlaylistBtnY);
        DrawTextureAt(plRenderer, plTexUp, kPlaylistUpX, kPlaylistBtnY);
        DrawTextureAt(plRenderer, plTexDown, kPlaylistDownX, kPlaylistBtnY);
        DrawTextureAt(plRenderer, plTexSeek, kPlaylistSeekX, kPlaylistBtnY);
        if (plPressedButton >= 0) {
            SDL_SetRenderDrawColor(plRenderer, 0x3d, 0xff, 0x74, 255);
            SDL_Rect hi{kPlButtonX[plPressedButton] - 1, kPlaylistBtnY - 1, kPlaylistBtnW + 2, kPlaylistBtnH + 2};
            SDL_RenderDrawRect(plRenderer, &hi);
        }

        // xmDInfo(0)/(1): two status panels, visible in a real screenshot -
        // an earlier pass didn't implement these at all. There's no
        // scrolling-log source to feed them yet (WriteINFO's equivalent
        // doesn't exist here), so they show real-but-minimal state instead
        // of inventing placeholder log text.
        {
            DrawInsetPanel(plRenderer, kPlInfoX, kPlInfoY0, kPlInfoW, kPlInfoH);
            DrawInsetPanel(plRenderer, kPlInfoX, kPlInfoY1, kPlInfoW, kPlInfoH);

            char line0[48];
            std::snprintf(line0, sizeof(line0), "%d TRACKS", static_cast<int>(playlist.size()));
            DrawTextureAt(plRenderer, plInfo0TextCache.Get(plRenderer, font, line0, kPlInfoW - 4), kPlInfoX + 3,
                          kPlInfoY0 + 3);

            std::string line1 = "NO TRACK OPEN";
            if (engine.channels() != 0) {
                char buf[48];
                std::snprintf(buf, sizeof(buf), "%uHZ %uCH", engine.sampleRate(), engine.channels());
                line1 = buf;
            }
            DrawTextureAt(plRenderer, plInfo1TextCache.Get(plRenderer, font, line1, kPlInfoW - 4), kPlInfoX + 3,
                          kPlInfoY1 + 3);
        }

        if (plSaveMenuOpen) {
            // Quick Save/Save As popup - see the layout comment on
            // kPlSaveMenuX/Y for why it has to overlap the bottom of the
            // list rather than sit in genuinely free space. Bevel::Draw
            // fills an opaque background before bordering, so this fully
            // obscures whatever list rows are underneath while open.
            Bevel::Draw(plRenderer, kPlSaveMenuX, kPlSaveMenuY, kPlSaveMenuW, kPlSaveMenuH);
            static const char* kSaveMenuItems[2] = {"QUICK SAVE", "SAVE AS..."};
            for (int i = 0; i < 2; ++i) {
                const int iy = kPlSaveMenuY + i * kPlSaveMenuItemH;
                DrawTextureAt(plRenderer,
                              plSaveMenuTextCache[static_cast<size_t>(i)].Get(plRenderer, font, kSaveMenuItems[i],
                                                                               10 * gfx::BitmapFont::kCellW),
                              kPlSaveMenuX + 2, iy + (kPlSaveMenuItemH - gfx::BitmapFont::kCellH) / 2);
            }
            SDL_SetRenderDrawColor(plRenderer, 0x23, 0x26, 0x20, 255);
            SDL_RenderDrawLine(plRenderer, kPlSaveMenuX + 1, kPlSaveMenuY + kPlSaveMenuItemH,
                                kPlSaveMenuX + kPlSaveMenuW - 2, kPlSaveMenuY + kPlSaveMenuItemH);
        }

        if (plClearConfirmOpen) {
            // "Are you sure?" popup for Clear - same popup style as the
            // Save menu above (Bevel::Draw fully obscures the list rows
            // underneath while open). Row 0 is an inert label, not a
            // button - only YES (1) and NO (2) act, see
            // handlePlaylistClickAt.
            Bevel::Draw(plRenderer, kPlClearConfirmX, kPlClearConfirmY, kPlClearConfirmW, kPlClearConfirmH);
            static const char* kClearConfirmItems[3] = {"CLEAR PLAYLIST?", "YES", "NO"};
            static const int kClearConfirmItemChars[3] = {15, 3, 2};
            for (int i = 0; i < 3; ++i) {
                const int iy = kPlClearConfirmY + i * kPlSaveMenuItemH;
                DrawTextureAt(plRenderer,
                              plClearConfirmTextCache[static_cast<size_t>(i)].Get(
                                  plRenderer, font, kClearConfirmItems[i],
                                  kClearConfirmItemChars[i] * gfx::BitmapFont::kCellW),
                              kPlClearConfirmX + 2, iy + (kPlSaveMenuItemH - gfx::BitmapFont::kCellH) / 2);
                if (i < 2) {
                    SDL_SetRenderDrawColor(plRenderer, 0x23, 0x26, 0x20, 255);
                    SDL_RenderDrawLine(plRenderer, kPlClearConfirmX + 1, iy + kPlSaveMenuItemH,
                                        kPlClearConfirmX + kPlClearConfirmW - 2, iy + kPlSaveMenuItemH);
                }
            }
        }
    };

    // Handles a click at logical (lx, ly) inside the playlist window;
    // returns true if it hit something. Shared by the real mouse handler
    // and the --playlist-click/--select-row debug hooks below.
    auto handlePlaylistClickAt = [&](int lx, int ly) {
        if (plClearConfirmOpen) {
            // Same hit-test-first reasoning as the Save menu below - and
            // mutually exclusive with it by construction (see
            // handlePlaylistButtonPress), so checking this one first is
            // safe regardless of which popup (if either) is actually open.
            if (lx >= kPlClearConfirmX && lx < kPlClearConfirmX + kPlClearConfirmW && ly >= kPlClearConfirmY &&
                ly < kPlClearConfirmY + kPlClearConfirmH) {
                const int item = (ly - kPlClearConfirmY) / kPlSaveMenuItemH; // 0=label, 1=YES, 2=NO
                plClearConfirmOpen = false;
                if (item == 1) {
                    clearPlaylist();
                } else if (item == 0) {
                    // Clicked the inert label row - not a button, leave
                    // the popup open (undo the close above) rather than
                    // silently dismissing on a non-actionable click.
                    plClearConfirmOpen = true;
                }
                // item == 2 (NO) falls through with the popup already closed.
            } else {
                plClearConfirmOpen = false; // click outside the open popup: dismiss only
            }
            return;
        }
        if (plSaveMenuOpen) {
            // Menu hit-test runs before anything else so a click meant for
            // the popup never also falls through to a button/list row
            // underneath it (the menu necessarily overlaps the bottom of
            // the list - see kPlSaveMenuX/Y's comment in layout.h).
            if (lx >= kPlSaveMenuX && lx < kPlSaveMenuX + kPlSaveMenuW && ly >= kPlSaveMenuY &&
                ly < kPlSaveMenuY + kPlSaveMenuH) {
                const int item = (ly - kPlSaveMenuY) / kPlSaveMenuItemH; // 0=Quick Save, 1=Save As...
                plSaveMenuOpen = false;
                if (item == 0) {
                    quickSavePlaylist();
                } else if (item == 1) {
                    // No existing basename utility in this codebase to
                    // reuse - a save dialog's "default name" wants just the
                    // filename, not the full quick-save path.
                    const auto slash = plQuickSavePath.find_last_of('/');
                    const std::string defaultName =
                        slash == std::string::npos ? plQuickSavePath : plQuickSavePath.substr(slash + 1);
                    if (auto chosen = app::SaveNativeFileDialog(defaultName)) saveAsToPath(*chosen);
                }
            } else {
                plSaveMenuOpen = false; // click outside the open menu: dismiss only
            }
            return;
        }
        for (int i = 0; i < 6; ++i) {
            if (lx >= kPlButtonX[i] && lx < kPlButtonX[i] + kPlaylistBtnW && ly >= kPlaylistBtnY &&
                ly < kPlaylistBtnY + kPlaylistBtnH) {
                plPressedButton = i;
                handlePlaylistButtonPress(i);
                return;
            }
        }
        if (lx >= kPlaylistListX && lx < kPlaylistListX + kPlaylistListW && ly >= kPlaylistListY &&
            ly < kPlaylistListY + kPlaylistListH) {
            const int row = (ly - kPlaylistListY) / kPlaylistRowH + plScrollOffset;
            if (row >= 0 && static_cast<size_t>(row) < playlist.size()) {
                const Uint32 now = SDL_GetTicks();
                const bool doubleClick = (row == lastClickedRow) && (now - lastClickTimeMs < 400);
                lastClickedRow = row;
                lastClickTimeMs = now;
                plSelected = row;
                if (doubleClick) {
                    // Mirrors ListaMp3_DblClick: play immediately regardless of state.
                    openPlaylistIndex(row);
                } else if (engine.state() != audio::PlayState::Playing) {
                    // Mirrors ListaMp3_Click: only arms the index if nothing is playing.
                    playlist.SetCurrentIndex(row);
                }
            }
        }
    };

    auto lowerExt = [](const std::string& path) {
        const auto dot = path.find_last_of('.');
        std::string ext = dot == std::string::npos ? "" : path.substr(dot + 1);
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });
        return ext;
    };

    // Mirrors GestioneExtDrag + ElaboraFileDrop: a dropped .m3u replaces the
    // whole playlist; a dropped audio file is appended and selected, but -
    // faithfully - does NOT auto-play (ElaboraFileDrop never calls
    // PlayStream). One divergence, noted rather than hidden: the original
    // inspects only the *first* dropped file's extension to decide
    // playlist-replace vs. append for the whole batch; SDL delivers one
    // SDL_DROPFILE event per file with no batch grouping, so this decides
    // per file instead.
    auto handleDroppedFile = [&](const std::string& path) {
        const std::string ext = lowerExt(path);
        if (ext == "m3u") {
            // Mirrors GestioneExtDrag -> PulisciTutto -> StopAll: a dropped
            // m3u replaces the whole list, so whatever was open no longer
            // belongs to it - fully release it, not just Stop() (see
            // engine.Close()'s doc comment for why Stop() alone isn't
            // enough here).
            engine.Close();
            playlist.Clear();
            playlist.LoadM3U(path);
            plSelected = playlist.empty() ? -1 : static_cast<int>(playlist.size()) - 1;
        } else if (ext == "mp3" || ext == "flac") {
            playlist.Add(path);
            plSelected = static_cast<int>(playlist.size()) - 1;
            ensureRowVisible(plSelected);
        } else {
            // TODO: "open all the .mp3s or .flacs [if] a directory is
            // selected" - a dropped/chosen folder (drag-and-drop already
            // passes one straight through unchanged; OpenNativeFileDialog
            // now allows picking a folder too, for the same reason) adds
            // every .mp3/.flac directly inside it, sorted alphabetically
            // to match Finder's default order. Not recursive - matches
            // the TODO's literal wording, and avoids a surprise mass
            // import from an accidentally-dropped parent folder.
            std::error_code ec;
            if (std::filesystem::is_directory(path, ec)) {
                std::vector<std::string> found;
                try {
                    for (const auto& entry : std::filesystem::directory_iterator(path)) {
                        if (!entry.is_regular_file()) continue;
                        const std::string entryExt = lowerExt(entry.path().string());
                        if (entryExt == "mp3" || entryExt == "flac") found.push_back(entry.path().string());
                    }
                } catch (const std::filesystem::filesystem_error&) {
                    // Keep whatever was found before a permission error or
                    // similar mid-iteration hiccup - partial results beat
                    // none.
                }
                std::sort(found.begin(), found.end());
                for (const auto& f : found) playlist.Add(f);
                if (!found.empty()) {
                    plSelected = static_cast<int>(playlist.size()) - 1;
                    ensureRowVisible(plSelected);
                }
            }
        }
    };

    // Eject: OpenMp3dlg equivalent - native file/folder picker, each
    // chosen path fed through the exact same add-to-playlist logic a
    // drag-and-drop uses (handleDroppedFile, including its directory
    // expansion), so multi-select, m3u/mp3/flac handling, and "a whole
    // folder was picked" all stay in one place.
    onAddFilesRequested = [&]() {
        for (const std::string& path : app::OpenNativeFileDialogFiles()) handleDroppedFile(path);
    };
    onAddFolderRequested = [&]() {
        for (const std::string& path : app::OpenNativeFileDialogFolder()) handleDroppedFile(path);
    };

    // ---- EQ window (frmEQ.frm) ----
    SDL_Texture* eqTexUp = UploadTexture(eqRenderer, skin.volUp);
    SDL_Texture* eqTexDown = UploadTexture(eqRenderer, skin.volDown);

    // Mirrors optEQ_Click's five preset arrays exactly (values, not
    // maxed to 127 - fed through the same -127..127 -> +-12dB mapping as a
    // manually-dragged slider). Labels shortened from the original's
    // "Bass Boost"/"Treble Boost" captions to fit the horizontal preset
    // row's 60px-wide buttons at this font's fixed 5px/char width.
    static constexpr const char* kEqPresetNames[5] = {"NORMAL", "ROCK", "POP", "BASS", "TREBLE"};
    static constexpr int kEqPresetValues[5][audio::Equalizer::kBands] = {
        {0, 0, 0, 0, 0, 0, 0, 0, 0, 0},         {60, 40, 20, 0, -20, -20, 0, 20, 40, 60},
        {20, 30, 40, 60, 60, 40, 30, 20, 0, 0},  {60, 80, 40, 20, 0, 0, 0, 0, 0, 0},
        {0, 0, 0, 0, 20, 30, 40, 60, 60, 60},
    };
    static constexpr const char* kEqBandLabels[audio::Equalizer::kBands] = {"60",  "170", "310", "600", "1K",
                                                                             "3K",  "6K",  "12K", "14K", "16K"};
    // -1 = none (matches Form_Load never checking a radio button); restored
    // from the saved session (TODO: "EQ mode not saved") so the correct
    // preset button stays highlighted across a relaunch, same as the band
    // gains themselves are restored above.
    int eqCurrentPreset = resumeSession ? std::clamp(sessionSettings.eqPreset, -1, 4) : -1;
    int eqPressedSlider = -1; // band index whose arrow is held, or -1
    int eqPressedPreset = -1;

    // TODO: "per song equalization setting". Off = today's behavior, one
    // global set of bands applies to every track. On = each track's own
    // bands are recalled on open (openPlaylistIndex, above) and any edit
    // made here is written back into perSongEqBands for whichever track
    // is currently open (syncPerSongEqIfEnabled, below) - see
    // session.h's EqPerSongPath()/SerializeEqPerSong for the on-disk
    // format. A track with no entry yet starts flat (all-zero), not
    // inherited from whatever was playing before.
    bool perSongEqEnabled = sessionSettings.perSongEq;
    std::unordered_map<std::string, std::array<int, audio::Equalizer::kBands>> perSongEqBands;
    app::LoadEqPerSongFile(app::EqPerSongPath(), perSongEqBands); // no warning if missing - first run has none yet

    applyPerSongEqForCurrentTrack = [&]() {
        if (!perSongEqEnabled || playlist.empty() || playlist.currentIndex() < 0) return;
        const auto it = perSongEqBands.find(playlist.at(static_cast<size_t>(playlist.currentIndex())));
        for (int b = 0; b < audio::Equalizer::kBands; ++b) {
            engine.SetEqBand(b, it != perSongEqBands.end() ? it->second[static_cast<size_t>(b)] : 0);
        }
        eqCurrentPreset = -1;
    };
    // Startup auto-open (src/main.cpp's resumeSession/positional-tracks
    // branches) opens tracks via engine.Open() directly rather than
    // through openPlaylistIndex, same gap notifyNowPlayingChanged already
    // had to work around - catches up immediately instead of waiting for
    // the next real track switch.
    applyPerSongEqForCurrentTrack();

    auto eqTrackInnerY = [&](int* top, int* bottom) {
        *top = kEqSliderTrackY + kEqArrowSize;
        *bottom = kEqSliderTrackY + kEqSliderTrackH - kEqArrowSize;
    };

    // Writes the engine's current 10 bands into perSongEqBands for
    // whichever track is open, when per-song mode is on - called after
    // every user-driven band change (preset pick, slider drag, arrow
    // click) so the file saved on exit always reflects the last edit,
    // not just whatever was current at startup.
    auto syncPerSongEqIfEnabled = [&]() {
        if (!perSongEqEnabled || playlist.empty() || playlist.currentIndex() < 0) return;
        std::array<int, audio::Equalizer::kBands> b{};
        for (int i = 0; i < audio::Equalizer::kBands; ++i) b[i] = engine.EqBand(i);
        perSongEqBands[playlist.at(static_cast<size_t>(playlist.currentIndex()))] = b;
    };

    auto applyEqPreset = [&](int idx) {
        if (idx < 0 || idx >= 5) return;
        for (int b = 0; b < audio::Equalizer::kBands; ++b) engine.SetEqBand(b, kEqPresetValues[idx][b]);
        eqCurrentPreset = idx;
        syncPerSongEqIfEnabled();
    };

    // Click-to-jump rather than a continuous drag (the original's vsGraphic
    // supports dragging the thumb; this is a documented simplification -
    // still lands on the exact target value, just needs a click at that
    // height instead of a drag gesture).
    auto handleEqSliderClickAt = [&](int band, int ly) {
        int top, bottom;
        eqTrackInnerY(&top, &bottom);
        const int clamped = std::clamp(ly, top, bottom);
        const double frac = static_cast<double>(clamped - top) / (bottom - top); // 0=top, 1=bottom
        const int value = static_cast<int>(std::lround((1.0 - 2.0 * frac) * 127.0)); // top=+127
        engine.SetEqBand(band, value);
        eqCurrentPreset = -1; // manual tweak breaks preset match, as in the original
        syncPerSongEqIfEnabled();
    };

    auto handleEqClickAt = [&](int lx, int ly) {
        for (int i = 0; i < 5; ++i) {
            const int bx = kEqPresetX0 + i * (kEqPresetBtnW + kEqPresetGap);
            if (lx >= bx && lx < bx + kEqPresetBtnW && ly >= kEqPresetY && ly < kEqPresetY + kEqPresetH) {
                eqPressedPreset = i;
                applyEqPreset(i);
                return;
            }
        }
        if (lx >= kEqPerSongX && lx < kEqPerSongX + kEqPerSongW && ly >= kEqPerSongY &&
            ly < kEqPerSongY + kEqPerSongH) {
            perSongEqEnabled = !perSongEqEnabled;
            // Felt immediately rather than only on the next track change -
            // reuses the exact same lookup-or-flat logic openPlaylistIndex
            // triggers on a real track switch. A no-op when turning off
            // (the guard inside only ever applies bands when enabled), so
            // the global bands are simply left as whatever they already
            // were.
            applyPerSongEqForCurrentTrack();
            return;
        }
        for (int b = 0; b < audio::Equalizer::kBands; ++b) {
            const int sx = kEqSliderX0 + b * kEqSliderPitch;
            if (lx < sx || lx >= sx + kEqSliderW) continue;
            if (ly >= kEqSliderTrackY && ly < kEqSliderTrackY + kEqArrowSize) {
                eqPressedSlider = b;
                engine.SetEqBand(b, std::clamp(engine.EqBand(b) + 13, -127, 127));
                eqCurrentPreset = -1;
                syncPerSongEqIfEnabled();
                return;
            }
            if (ly >= kEqSliderTrackY + kEqSliderTrackH - kEqArrowSize && ly < kEqSliderTrackY + kEqSliderTrackH) {
                eqPressedSlider = b;
                engine.SetEqBand(b, std::clamp(engine.EqBand(b) - 13, -127, 127));
                eqCurrentPreset = -1;
                syncPerSongEqIfEnabled();
                return;
            }
            if (ly >= kEqSliderTrackY && ly < kEqSliderTrackY + kEqSliderTrackH) {
                eqPressedSlider = b;
                handleEqSliderClickAt(b, ly);
                return;
            }
        }
    };

    CachedTextTexture eqTitleTextCache;
    std::array<CachedTextTexture, 3> eqLegendTextCache;
    std::array<CachedTextTexture, audio::Equalizer::kBands> eqBandTextCache;
    std::array<CachedTextTexture, 5> eqPresetTextCache;
    CachedTextTexture eqPerSongTextCache;

    auto drawEqFrame = [&]() {
        SDL_SetRenderDrawColor(eqRenderer, 0x10, 0x12, 0x09, 255);
        SDL_RenderClear(eqRenderer);
        Bevel::Draw(eqRenderer, 0, 0, kEqWindowW, kEqWindowH);
        DrawWindowHeader(eqRenderer, kEqWindowW, kDragStripH);
        {
            DrawTextureAt(eqRenderer, eqTitleTextCache.Get(eqRenderer, font, "EQUALIZER", 9 * gfx::BitmapFont::kCellW),
                          8, (kDragStripH - gfx::BitmapFont::kCellH) / 2);
        }
        DrawCloseIcon(eqRenderer, kCloseX, kCloseY, kCloseSize);

        int top, bottom;
        eqTrackInnerY(&top, &bottom);

        // dB legend (matches the "+12db"/"0db"/"-12db" labels in Form_Load).
        {
            const char* labels[3] = {"+12", "0", "-12"};
            const int ys[3] = {top - 3, (top + bottom) / 2 - 3, bottom - 3};
            for (int i = 0; i < 3; ++i) {
                DrawTextureAt(eqRenderer,
                              eqLegendTextCache[static_cast<size_t>(i)].Get(
                                  eqRenderer, font, labels[i],
                                  static_cast<int>(std::string(labels[i]).size()) * gfx::BitmapFont::kCellW),
                              kEqLegendX, ys[i]);
            }
        }

        for (int b = 0; b < audio::Equalizer::kBands; ++b) {
            const int sx = kEqSliderX0 + b * kEqSliderPitch;
            DrawTextureAt(eqRenderer, eqTexUp, sx + (kEqSliderW - kEqArrowSize) / 2, kEqSliderTrackY);
            DrawTextureAt(eqRenderer, eqTexDown, sx + (kEqSliderW - kEqArrowSize) / 2,
                          kEqSliderTrackY + kEqSliderTrackH - kEqArrowSize);

            SDL_SetRenderDrawColor(eqRenderer, 0xce, 0xce, 0xce, 255);
            const int cx = sx + kEqSliderW / 2;
            SDL_RenderDrawLine(eqRenderer, cx - 3, top, cx - 3, bottom);
            SDL_RenderDrawLine(eqRenderer, cx + 3, top, cx + 3, bottom);

            const int value = engine.EqBand(b);
            const double frac = (127.0 - value) / 254.0; // 0=top(+127), 1=bottom(-127)
            const int thumbY = top + static_cast<int>(frac * (bottom - top)) - 3;
            SDL_SetRenderDrawColor(eqRenderer, 0x00, 0xff, 0x00, 255);
            SDL_Rect thumb{sx + 1, thumbY, kEqSliderW - 2, 6};
            SDL_RenderFillRect(eqRenderer, &thumb);

            if (eqPressedSlider == b) {
                SDL_SetRenderDrawColor(eqRenderer, 0x3d, 0xff, 0x74, 255);
                SDL_Rect hi{sx - 1, kEqSliderTrackY - 1, kEqSliderW + 2, kEqSliderTrackH + 2};
                SDL_RenderDrawRect(eqRenderer, &hi);
            }

            const std::string label = kEqBandLabels[b];
            DrawTextureAt(eqRenderer,
                          eqBandTextCache[static_cast<size_t>(b)].Get(
                              eqRenderer, font, label, static_cast<int>(label.size()) * gfx::BitmapFont::kCellW),
                          sx + 1, kEqFreqLabelY);
        }

        for (int i = 0; i < 5; ++i) {
            const int bx = kEqPresetX0 + i * (kEqPresetBtnW + kEqPresetGap);
            const bool active = (eqCurrentPreset == i);
            // Same fix/palette as the PL/EQ toggle buttons on Main - see
            // drawToggle's comment.
            SDL_SetRenderDrawColor(eqRenderer, active ? 0x2a : 0x1a, active ? 0x8f : 0x5a, active ? 0x46 : 0x2a,
                                    255);
            SDL_Rect bg{bx, kEqPresetY, kEqPresetBtnW, kEqPresetH};
            SDL_RenderFillRect(eqRenderer, &bg);
            SDL_SetRenderDrawColor(eqRenderer, active ? 0x3d : 0x2a, active ? 0xff : 0x7a, active ? 0x74 : 0x3a,
                                    255);
            SDL_RenderDrawRect(eqRenderer, &bg);

            const std::string label = kEqPresetNames[i];
            const int textX = bx + (kEqPresetBtnW - static_cast<int>(label.size()) * gfx::BitmapFont::kCellW) / 2;
            DrawTextureAt(eqRenderer,
                          eqPresetTextCache[static_cast<size_t>(i)].Get(
                              eqRenderer, font, label, static_cast<int>(label.size()) * gfx::BitmapFont::kCellW),
                          textX, kEqPresetY + 6);
        }

        // Per-song EQ toggle - same active/inactive fill+outline
        // convention as the preset buttons above and drawToggle on Main.
        {
            SDL_SetRenderDrawColor(eqRenderer, perSongEqEnabled ? 0x2a : 0x1a, perSongEqEnabled ? 0x8f : 0x5a,
                                    perSongEqEnabled ? 0x46 : 0x2a, 255);
            SDL_Rect bg{kEqPerSongX, kEqPerSongY, kEqPerSongW, kEqPerSongH};
            SDL_RenderFillRect(eqRenderer, &bg);
            SDL_SetRenderDrawColor(eqRenderer, perSongEqEnabled ? 0x3d : 0x2a, perSongEqEnabled ? 0xff : 0x7a,
                                    perSongEqEnabled ? 0x74 : 0x3a, 255);
            SDL_RenderDrawRect(eqRenderer, &bg);

            const std::string label = "XEQ";
            const int textX = kEqPerSongX + (kEqPerSongW - static_cast<int>(label.size()) * gfx::BitmapFont::kCellW) / 2;
            DrawTextureAt(eqRenderer,
                          eqPerSongTextCache.Get(eqRenderer, font, label,
                                                  static_cast<int>(label.size()) * gfx::BitmapFont::kCellW),
                          textX, kEqPerSongY + (kEqPerSongH - gfx::BitmapFont::kCellH) / 2);
        }
    };

    // Info window content: everything genuinely available from the open
    // decoder (see kInfoWindowW's comment for what the original showed that
    // isn't available here). Average bit rate is computed from file size /
    // duration (exact for CBR, an honest approximation for VBR - the
    // original's "VBR" flag came from parsing MPEG frame headers, which
    // dr_mp3 doesn't expose), not read from a header field.
    CachedTextTexture infoTitleTextCache;
    std::array<CachedTextTexture, 12> infoLineTextCache; // >= max lines drawInfoFrame ever produces

    auto drawInfoFrame = [&]() {
        SDL_SetRenderDrawColor(infoRenderer, 0x10, 0x12, 0x09, 255);
        SDL_RenderClear(infoRenderer);
        Bevel::Draw(infoRenderer, 0, 0, kInfoWindowW, kInfoWindowH);
        DrawWindowHeader(infoRenderer, kInfoWindowW, kDragStripH);
        {
            DrawTextureAt(infoRenderer,
                          infoTitleTextCache.Get(infoRenderer, font, "INFORMATION", 11 * gfx::BitmapFont::kCellW), 8,
                          (kDragStripH - gfx::BitmapFont::kCellH) / 2);
        }
        DrawCloseIcon(infoRenderer, kCloseX, kCloseY, kCloseSize);

        std::vector<std::string> lines;
        const bool infoTrackOpen = engine.channels() != 0 && playlist.currentIndex() >= 0;
        if (infoTrackOpen) {
            const std::string& path = playlist.at(static_cast<size_t>(playlist.currentIndex()));
            const bool isFlac = lowerExt(path) == "flac";
            lines.push_back("File: " + BaseName(path));
            // TODO: "reports ID3 values if present" - one line per field
            // actually populated (audio::ReadTrackTags), not a fixed slot
            // per field - a track with no tags at all just skips straight
            // to Format: below, same as before this feature existed.
            {
                const audio::TagInfo tags = audio::ReadTrackTags(path);
                if (!tags.title.empty()) lines.push_back("Title: " + tags.title);
                if (!tags.artist.empty()) lines.push_back("Artist: " + tags.artist);
                if (!tags.album.empty()) lines.push_back("Album: " + tags.album);
                if (!tags.genre.empty()) lines.push_back("Genre: " + tags.genre);
                if (!tags.track.empty()) lines.push_back("Track: " + tags.track);
            }
            lines.push_back(std::string("Format: ") + (isFlac ? "FLAC" : "MP3"));
            lines.push_back(std::string("Mode: ") + (engine.channels() == 1 ? "Mono" : "Stereo"));
            lines.push_back("Frequency: " + std::to_string(engine.sampleRate()) + " Hz");
            {
                // Same helper Main's own BitRate readout now uses (see its
                // comment/the TODO it replaces) - was duplicated inline
                // here before that existed.
                const int kbps = ComputeAvgBitrateKbps(path, engine.durationSeconds());
                lines.push_back(kbps > 0 ? "Bit Rate (avg): " + std::to_string(kbps) + " Kbit/s"
                                          : "Bit Rate (avg): n/a");
            }
            {
                const int durSec = static_cast<int>(engine.durationSeconds());
                char buf[32];
                std::snprintf(buf, sizeof(buf), "Duration: %02d:%02d", durSec / 60, durSec % 60);
                lines.push_back(buf);
            }
            lines.push_back(std::string("Decoder: ") + (isFlac ? "dr_flac" : "dr_mp3"));
        } else {
            lines.push_back("No track loaded.");
        }

        int ly = kInfoTextY0;
        for (size_t i = 0; i < lines.size(); ++i) {
            const std::string& line = lines[i];
            DrawTextureAt(infoRenderer,
                          infoLineTextCache[i].Get(infoRenderer, font, line,
                                                    static_cast<int>(line.size()) * gfx::BitmapFont::kCellW),
                          kInfoTextX, ly);
            ly += kInfoLineH;
        }
    };

    // About window content (TODO: "create about page linked to the
    // japanese character click ... with this logo ... and version").
    // Purely static branding - not present in the original at all (see
    // layout.h's kAboutWindow* comment) - so unlike drawInfoFrame there's
    // no per-track state to read; the only two rendered strings are the
    // app name/version (same formatted version string as texStatus on
    // Main) and the credit line. The icon and Zolnetwork mark are the two
    // pre-flattened bitmaps described on Skin::aboutIcon/aboutZLogo.
    CachedTextTexture aboutTitleTextCache, aboutHeadingTextCache, aboutVersionTextCache, aboutCreditTextCache;

    auto drawAboutFrame = [&]() {
        SDL_SetRenderDrawColor(aboutRenderer, 0x10, 0x12, 0x09, 255);
        SDL_RenderClear(aboutRenderer);
        Bevel::Draw(aboutRenderer, 0, 0, kAboutWindowW, kAboutWindowH);
        DrawWindowHeader(aboutRenderer, kAboutWindowW, kDragStripH);
        {
            DrawTextureAt(aboutRenderer,
                          aboutTitleTextCache.Get(aboutRenderer, font, "ABOUT", 5 * gfx::BitmapFont::kCellW), 8,
                          (kDragStripH - gfx::BitmapFont::kCellH) / 2);
        }
        DrawCloseIcon(aboutRenderer, kCloseX, kCloseY, kCloseSize);

        DrawTextureAt(aboutRenderer, texAboutIcon, kAboutIconX, kAboutIconY);
        {
            const std::string title = "X-MAD.PLAYER REVIVAL";
            DrawTextureAt(aboutRenderer,
                          aboutHeadingTextCache.Get(aboutRenderer, font, title,
                                                     static_cast<int>(title.size()) * gfx::BitmapFont::kCellW),
                          kAboutTitleX, kAboutTitleY);
        }
        {
            // Same formatted version string as Main's status line
            // (texStatus) - deliberately kept in sync rather than
            // hardcoded twice.
            const std::string versionLine = std::string("*** V") + app::kVersion + " BETA GOJIRA ***";
            DrawTextureAt(aboutRenderer,
                          aboutVersionTextCache.Get(aboutRenderer, font, versionLine,
                                                     static_cast<int>(versionLine.size()) * gfx::BitmapFont::kCellW),
                          kAboutVersionX, kAboutVersionY);
        }
        DrawDashedHLine(aboutRenderer, 8, kAboutWindowW - 8, kAboutDividerY, 0x3d, 0xff, 0x74);

        {
            int zw, zh;
            SDL_QueryTexture(texAboutZLogo, nullptr, nullptr, &zw, &zh);
            DrawTextureAt(aboutRenderer, texAboutZLogo, (kAboutWindowW - zw) / 2, kAboutZLogoY);
        }
        {
            const std::string credit = "FHT - ZOLNETWORK";
            const int cw = static_cast<int>(credit.size()) * gfx::BitmapFont::kCellW;
            DrawTextureAt(aboutRenderer, aboutCreditTextCache.Get(aboutRenderer, font, credit, cw),
                          (kAboutWindowW - cw) / 2, kAboutCreditY);
        }
    };

    for (int i = 0; i < utilClickCount; ++i) {
        // Debug hook: drives handleUtilityPress through the exact same
        // lambda the real mouse handler calls.
        pressedUtility = 2; // SpecMode's index in utilityButtons
        handleUtilityPress(UtilityAction::SpecMode);
    }
    if (utilClickCount > 0) {
        std::cout << "visMode after " << utilClickCount << " SpecMode click(s): " << static_cast<int>(visMode)
                  << " (0=PeakFalls, 1=NoPeakFalls, 2=PeakNoFalls, 3=FadeFft, 4=Oscilloscope, "
                     "5=StereoOscilloscope)\n";
    }

    if (clickName == "mute") {
        // Debug hook: drives handleUtilityPress through the exact same
        // lambda the real mouse handler calls.
        pressedUtility = 1; // Mute's index in utilityButtons
        handleUtilityPress(UtilityAction::Mute);
        std::cout << "muted=" << engine.IsMuted() << "\n";
    } else if (clickName == "info") {
        // Debug hook: drives handleUtilityPress through the exact same
        // lambda the real mouse handler calls.
        pressedUtility = 3; // Info's index in utilityButtons
        handleUtilityPress(UtilityAction::Info);
        std::cout << "infoVisible=" << infoUserVisible << "\n";
    } else if (clickName == "maximize") {
        // Debug hook: drives the exact same lambda the real click handler calls.
        toggleMaximize();
        std::cout << "plVisible=" << plUserVisible << " eqVisible=" << eqUserVisible << "\n";
    } else if (clickName == "about") {
        // Debug hook: drives the exact same lambda the real shock-icon
        // click handler calls (see kShockX's hit-test below).
        toggleSecondaryWindow(aboutWindow, aboutUserVisible, aboutMinimized);
        std::cout << "aboutVisible=" << aboutUserVisible << "\n";
    } else if (!clickName.empty()) {
        // Debug hook: drives handleTransportPress through the exact same
        // lambda the real mouse handler calls, so this exercises the real
        // code path (not a reimplementation of it) without needing a
        // physical click in a headless environment.
        static const std::pair<const char*, int> kNames[] = {{"back", 0},  {"play", 1}, {"stop", 2},
                                                               {"next", 3}, {"pause", 4}, {"eject", 5}};
        int idx = -1;
        for (auto& [name, i] : kNames)
            if (clickName == name) idx = i;
        if (idx < 0) {
            std::cerr << "unknown --click target '" << clickName << "'\n";
            return 1;
        }
        std::cout << "state before click: " << static_cast<int>(engine.state()) << "\n";
        pressedButton = idx;
        handleTransportPress(transportButtons[idx].action);
        std::cout << "state after click: " << static_cast<int>(engine.state())
                  << " position=" << engine.positionSeconds() << "\n";
    }
    if (setVolumeArg >= 0) {
        // Debug hook: drives the exact same slider-click math the real
        // mouse handler uses (handleVolSliderClickAt), not SetVolume
        // directly.
        const int top = kVolSliderY + kVolArrowSize;
        const int bottom = kVolSliderY + kVolSliderH - kVolArrowSize;
        const float frac = 1.0f - std::clamp(setVolumeArg, 0, 100) / 100.0f;
        const int ly = top + static_cast<int>(frac * (bottom - top));
        handleVolSliderClickAt(ly);
        std::cout << "volume -> " << std::lround(engine.Volume() * 100.0f) << " (requested " << setVolumeArg
                  << ")\n";
    }
    for (int i = 0; i < toggleXSoundCount; ++i) {
        // Debug hook: drives the exact same lambda the real "x" keypress calls.
        toggleXSound();
    }
    if (toggleXSoundCount > 0) {
        std::cout << "xSoundEnabled=" << engine.XSound() << "\n";
    }

    if (!dropPath.empty()) {
        // Debug hook: drives the exact same handler SDL_DROPFILE calls.
        std::cout << "playlist size before drop: " << playlist.size() << "\n";
        handleDroppedFile(dropPath);
        std::cout << "playlist size after drop: " << playlist.size() << " selected=" << plSelected << "\n";
    }

    if (selectRowArg >= 0) {
        // Debug hook: drives the exact same click-routing lambda the real
        // mouse handler uses, targeting the middle of that list row.
        const int ly = kPlaylistListY + selectRowArg * kPlaylistRowH + 1;
        handlePlaylistClickAt(kPlaylistListX + 1, ly);
        std::cout << "selected row " << selectRowArg << ", currentIndex=" << playlist.currentIndex() << "\n";
    }
    if (!playlistClickName.empty()) {
        static const std::pair<const char*, int> kPlNames[] = {
            {"clear", 0}, {"delete", 1}, {"save", 2}, {"up", 3}, {"down", 4}, {"seek", 5}};
        int idx = -1;
        for (auto& [name, i] : kPlNames)
            if (playlistClickName == name) idx = i;
        if (idx < 0) {
            std::cerr << "unknown --playlist-click target '" << playlistClickName << "'\n";
            return 1;
        }
        std::cout << "playlist size before: " << playlist.size() << " selected=" << plSelected << "\n";
        plPressedButton = idx;
        handlePlaylistButtonPress(idx);
        std::cout << "playlist size after: " << playlist.size() << " selected=" << plSelected
                  << " currentIndex=" << playlist.currentIndex() << "\n";
    }
    if (playlistQuickSaveTest) {
        // Debug hook: drives the exact same lambda the Quick Save menu
        // item calls - doesn't touch the popup's open/closed state at
        // all, just the write.
        quickSavePlaylist();
        std::ifstream check(plQuickSavePath);
        std::cout << "quick-save target=" << plQuickSavePath << " wrote=" << (check.good() ? "yes" : "no") << "\n";
    }
    if (!playlistSaveAsTestPath.empty()) {
        // Debug hook: drives the exact same lambda the Save As menu item
        // calls once the user has picked a path. The live NSSavePanel/
        // zenity dialog itself can't be driven headlessly, so this starts
        // from "the OS already returned this path" instead of trying to
        // script the dialog.
        saveAsToPath(playlistSaveAsTestPath);
        std::ifstream check(playlistSaveAsTestPath);
        std::cout << "save-as wrote to " << playlistSaveAsTestPath << " (" << (check.good() ? "ok" : "FAILED")
                   << "), quick-save target now=" << plQuickSavePath << "\n";
    }

    if (!clearReloadTestPath.empty()) {
        // Debug hook reproducing the exact reported scenario end-to-end
        // through the real production lambdas: Clear -> add a new track ->
        // press Play. Before engine.Close() existed, engine.channels() was
        // still nonzero after Clear (Stop() alone leaves the old decoder
        // loaded), so Play's "something is already open, restart it"
        // branch fired and resurrected the cleared track instead of
        // opening the newly-added one.
        std::cout << "clear-reload-test: before clear: playlist size=" << playlist.size()
                  << " engine.channels()=" << engine.channels() << " duration=" << engine.durationSeconds()
                  << "\n";
        clearPlaylist(); // Clear - bypasses the confirmation popup on purpose (see clearPlaylist's comment)
        std::cout << "clear-reload-test: after clear: playlist size=" << playlist.size()
                  << " engine.channels()=" << engine.channels() << "\n";
        handleDroppedFile(clearReloadTestPath); // add the new track (mirrors a drag-drop / Eject add)
        std::cout << "clear-reload-test: after add '" << clearReloadTestPath
                   << "': playlist size=" << playlist.size() << " currentIndex=" << playlist.currentIndex()
                   << "\n";
        handleTransportPress(TransportAction::Play);
        if (engine.channels() != 0) SDL_Delay(300); // let the decode thread + audio callback actually start
        std::cout << "clear-reload-test: after Play: engine.channels()=" << engine.channels()
                   << " duration=" << engine.durationSeconds() << " position=" << engine.positionSeconds()
                   << " state=" << static_cast<int>(engine.state()) << "\n";
    }

    if (eqSetBandArg >= 0) {
        // Debug hook: drives the exact same slider-click math the real
        // mouse handler uses (not SetEqBand directly), so this also
        // exercises handleEqSliderClickAt's Y->value mapping.
        int top, bottom;
        eqTrackInnerY(&top, &bottom);
        const double frac = (127.0 - eqSetValueArg) / 254.0;
        const int ly = top + static_cast<int>(frac * (bottom - top));
        handleEqSliderClickAt(eqSetBandArg, ly);
        std::cout << "band " << eqSetBandArg << " -> " << engine.EqBand(eqSetBandArg) << " (requested "
                  << eqSetValueArg << ")\n";
    }
    if (!eqPresetArg.empty()) {
        static const std::pair<const char*, int> kEqNames[] = {
            {"normal", 0}, {"rock", 1}, {"pop", 2}, {"bass", 3}, {"treble", 4}};
        int idx = -1;
        for (auto& [name, i] : kEqNames)
            if (eqPresetArg == name) idx = i;
        if (idx < 0) {
            std::cerr << "unknown --eq-preset target '" << eqPresetArg << "'\n";
            return 1;
        }
        applyEqPreset(idx);
        std::cout << "applied preset " << kEqPresetNames[idx] << ":";
        for (int b = 0; b < audio::Equalizer::kBands; ++b) std::cout << " " << engine.EqBand(b);
        std::cout << "\n";
    }

    auto dumpRenderTarget = [&](SDL_Renderer* r, int /*logicalW*/, int /*logicalH*/, const std::string& path) -> bool {
        // SDL_RenderReadPixels reads the renderer's actual backing-store
        // pixels, independent of any SDL_RenderSetScale we applied for
        // HiDPI - on a Retina display that's larger than our design-pixel
        // kWindowW/H*scale, so we must ask the renderer for its real output
        // size rather than trust the logical dimensions passed in.
        int w = 0, h = 0;
        SDL_GetRendererOutputSize(r, &w, &h);
        std::vector<uint8_t> pixels(static_cast<size_t>(w) * h * 4);
        if (SDL_RenderReadPixels(r, nullptr, SDL_PIXELFORMAT_RGBA32, pixels.data(), w * 4) != 0) {
            std::cerr << "SDL_RenderReadPixels failed: " << SDL_GetError() << "\n";
            return false;
        }
        std::ofstream out(path, std::ios::binary);
        out.write(reinterpret_cast<const char*>(&w), 4);
        out.write(reinterpret_cast<const char*>(&h), 4);
        out.write(reinterpret_cast<const char*>(pixels.data()), static_cast<std::streamsize>(pixels.size()));
        std::cout << "wrote " << path << " (" << w << "x" << h << ")\n";
        return true;
    };

    const Uint32 mainWindowID = SDL_GetWindowID(window);
    const Uint32 plWindowID = SDL_GetWindowID(plWindow);
    const Uint32 eqWindowID = SDL_GetWindowID(eqWindow);
    const Uint32 infoWindowID = SDL_GetWindowID(infoWindow);
    const Uint32 aboutWindowID = SDL_GetWindowID(aboutWindow);
    bool running = true;

    // Borderless windows have no OS title bar to drag by, so this is
    // hand-rolled: mouse-down in a window's drag strip (see kDragStripH)
    // that didn't land on a button starts a drag; global mouse coordinates
    // (not the event's window-relative ones) make the math correct even
    // once the window has moved out from under the original click point.
    struct DragState {
        SDL_Window* win = nullptr;
        bool active = false;
        int offsetX = 0, offsetY = 0;
        // Only ever populated for the main window's drag: windows currently
        // docked to it, each with its fixed (dx,dy) offset from main's
        // drag-start position. They're translated in lockstep with main
        // rather than independently snapped, so the whole docked group
        // moves as one rigid unit. Dragging EQ or Playlist directly never
        // populates this - moving either of them only moves itself and
        // undocks it, per the user's request.
        std::vector<std::pair<SDL_Window*, std::pair<int, int>>> followers;
    };
    DragState mainDrag{.win = window}, plDrag{.win = plWindow}, eqDrag{.win = eqWindow}, infoDrag{.win = infoWindow},
        aboutDrag{.win = aboutWindow};

    // How close an edge must be to a matching edge to count as *already
    // docked* (used to decide what follows main when it's dragged). Much
    // tighter than kSnapThreshold below, which governs when a drag starts
    // attracting toward a snap - once actually snapped, windows sit exactly
    // flush, so a tight tolerance avoids treating merely-nearby windows
    // (mid-drag, not yet snapped) as part of the rigid group.
    constexpr int kDockedTolerance = 2;

    auto beginDrag = [&](DragState& ds) {
        int mx, my, wx, wy;
        SDL_GetGlobalMouseState(&mx, &my);
        SDL_GetWindowPosition(ds.win, &wx, &wy);
        ds.active = true;
        ds.offsetX = mx - wx;
        ds.offsetY = my - wy;
        ds.followers.clear();
        if (ds.win == window) {
            std::vector<app::WinRect> all;
            std::vector<SDL_Window*> winPtrs;
            int rootIndex = -1;
            for (SDL_Window* w : {window, eqWindow, plWindow}) {
                if (SDL_GetWindowFlags(w) & SDL_WINDOW_HIDDEN) continue;
                int x, y, ww, hh;
                SDL_GetWindowPosition(w, &x, &y);
                SDL_GetWindowSize(w, &ww, &hh);
                if (w == window) rootIndex = static_cast<int>(all.size());
                all.push_back({x, y, ww, hh});
                winPtrs.push_back(w);
            }
            if (rootIndex >= 0) {
                for (int idx : app::FindDockedGroup(rootIndex, all, kDockedTolerance)) {
                    ds.followers.push_back({winPtrs[idx], {all[idx].x - wx, all[idx].y - wy}});
                }
            }
        }
    };
    // Magnetic window snapping (geometry lives in app::SnapDragPosition,
    // see window_snap.h/cpp - kept SDL-free there so it's independently
    // testable, since dragging itself reads live OS cursor position and
    // can't be driven through synthetic events the way everything else in
    // this file is verified). Same idea as Winamp's window docking: get an
    // edge within kSnapThreshold px of a matching edge on either of the
    // other two windows and it snaps flush.
    constexpr int kSnapThreshold = 14;
    auto updateDrag = [&](DragState& ds) {
        if (!ds.active) return;
        int mx, my;
        SDL_GetGlobalMouseState(&mx, &my);
        int w, h;
        SDL_GetWindowSize(ds.win, &w, &h);

        auto isFollower = [&](SDL_Window* w) {
            for (auto& f : ds.followers)
                if (f.first == w) return true;
            return false;
        };
        std::vector<app::WinRect> others;
        for (SDL_Window* other : {window, eqWindow, plWindow}) {
            if (other == ds.win || (SDL_GetWindowFlags(other) & SDL_WINDOW_HIDDEN)) continue;
            if (isFollower(other)) continue; // rigidly attached - not an independent snap target
            int ox, oy, ow, oh;
            SDL_GetWindowPosition(other, &ox, &oy);
            SDL_GetWindowSize(other, &ow, &oh);
            others.push_back({ox, oy, ow, oh});
        }
        auto snapped =
            app::SnapDragPosition(mx - ds.offsetX, my - ds.offsetY, w, h, others, kSnapThreshold);

        // Also snap to the screen's usable bounds (never the full display -
        // that would let a window's edge slide under the menu bar or behind
        // the Dock), so dragging near an edge sticks flush to it, and near a
        // corner sticks to both edges at once for free.
        SDL_Rect usable{};
        SDL_GetDisplayUsableBounds(SDL_GetWindowDisplayIndex(ds.win), &usable);
        snapped = app::SnapToScreenEdges(snapped.x, snapped.y, w, h,
                                          {usable.x, usable.y, usable.w, usable.h}, kSnapThreshold);

        SDL_SetWindowPosition(ds.win, snapped.x, snapped.y);
        for (auto& [followerWin, off] : ds.followers) {
            SDL_SetWindowPosition(followerWin, snapped.x + off.first, snapped.y + off.second);
        }
    };
    auto inRect = [](int lx, int ly, int rx, int ry, int rw, int rh) {
        return lx >= rx && lx < rx + rw && ly >= ry && ly < ry + rh;
    };

    // Declared here (rather than down by the loop that reads them) so
    // applyUiScale below can reach in and force a redraw after a rescale -
    // see its own trailing reset of these.
    std::optional<PlaylistFrameKey> lastPlKey;
    std::optional<EqFrameKey> lastEqKey;
    std::optional<InfoFrameKey> lastInfoKey;
    bool aboutDrawnOnce = false;

    // TODO: "Add a setting for UI scale / text+button size" - the live
    // rescale entry point, called from the View menu / Cmd+/-/0 (see
    // processEvent's kUiScaleEventType branch below). Re-stacks all 5
    // windows relative to Main's *current* position - if EQ/Playlist/Info/
    // About had been dragged elsewhere, a rescale re-docks them into the
    // standard column rather than trying to rescale an arbitrary undocked
    // offset. ApplyHiDpiRenderScale is safe to call repeatedly (just
    // re-queries the real backing-store size each time); CachedTextTexture
    // keys on logical text/field-width, never window pixel size, so it
    // needs no invalidation here.
    auto applyUiScale = [&](int requestedPercent) {
        uiScalePercent = SnapUiScalePercent(requestedPercent);
        scale = uiScalePercent / 100.0;
        int curMainX, curMainY;
        SDL_GetWindowPosition(window, &curMainX, &curMainY);
        const WindowLayout L = ComputeWindowLayout(scale, curMainX, curMainY);

        SDL_SetWindowSize(window, L.main.w, L.main.h);
        ApplyHiDpiRenderScale(renderer, app::layout::kWindowW, app::layout::kWindowH);

        SDL_SetWindowSize(eqWindow, L.eq.w, L.eq.h);
        SDL_SetWindowPosition(eqWindow, L.eq.x, L.eq.y);
        ApplyHiDpiRenderScale(eqRenderer, app::layout::kEqWindowW, app::layout::kEqWindowH);

        SDL_SetWindowSize(plWindow, L.playlist.w, L.playlist.h);
        SDL_SetWindowPosition(plWindow, L.playlist.x, L.playlist.y);
        ApplyHiDpiRenderScale(plRenderer, app::layout::kPlaylistWindowW, app::layout::kPlaylistWindowH);

        SDL_SetWindowSize(infoWindow, L.info.w, L.info.h);
        SDL_SetWindowPosition(infoWindow, L.info.x, L.info.y);
        ApplyHiDpiRenderScale(infoRenderer, app::layout::kInfoWindowW, app::layout::kInfoWindowH);

        SDL_SetWindowSize(aboutWindow, L.about.w, L.about.h);
        SDL_SetWindowPosition(aboutWindow, L.about.x, L.about.y);
        ApplyHiDpiRenderScale(aboutRenderer, app::layout::kAboutWindowW, app::layout::kAboutWindowH);
#ifdef __APPLE__
        app::SetUiScaleMenuChecked(uiScalePercent);
#endif
        // Playlist/EQ/Info only redraw+present when their content key
        // changes (see the main loop below) - none of those keys include
        // window size/scale, so without this a rescale that doesn't also
        // happen to change the current track/EQ bands/etc. leaves them
        // showing a stale frame in a freshly resized (and possibly, on a
        // compositor that resizes asynchronously, momentarily garbage-
        // filled) backing buffer. Forcing all three "dirty" here - plus
        // re-arming About's one-shot draw - guarantees the very next loop
        // iteration redraws every window at the new size regardless of
        // whether anything else changed.
        lastPlKey.reset();
        lastEqKey.reset();
        lastInfoKey.reset();
        aboutDrawnOnce = false;
    };

    // Shared by the real interactive loop below AND the --sim-click-*
    // debug hooks (see the dump-frame branch just below) - so a synthetic
    // event exercises the exact same SDL coordinate math and hit-testing a
    // real click does, not a hand-written imitation of it.
    auto processEvent = [&](const SDL_Event& ev) {
        if (ev.type == SDL_QUIT) running = false;
        if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE) running = false;
        if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_x && ev.key.repeat == 0) {
            toggleXSound();
        }
        if (ev.type == SDL_WINDOWEVENT && ev.window.event == SDL_WINDOWEVENT_CLOSE) {
            // Fallback in case the OS still offers some close path (Cmd+Q,
            // dock menu) even without a title bar - same semantics as our
            // own drawn close icons below: main quits, others just hide.
            if (ev.window.windowID == mainWindowID) running = false;
            else if (ev.window.windowID == plWindowID) { plUserVisible = false; SDL_HideWindow(plWindow); }
            else if (ev.window.windowID == eqWindowID) { eqUserVisible = false; SDL_HideWindow(eqWindow); }
            else if (ev.window.windowID == infoWindowID) { infoUserVisible = false; SDL_HideWindow(infoWindow); }
            else if (ev.window.windowID == aboutWindowID) { aboutUserVisible = false; SDL_HideWindow(aboutWindow); }
        }
        if (ev.type == SDL_WINDOWEVENT && ev.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
            // A window manager can resize a window's actual backing
            // surface asynchronously relative to our SDL_SetWindowSize
            // request in applyUiScale (observed on Wayland) -
            // ApplyHiDpiRenderScale's SDL_GetRendererOutputSize call right
            // after that request can then see the *old* size and compute a
            // render scale that's never revisited, leaving that window's
            // content confined to (or smeared past) the wrong fraction of
            // its new size - the "fonts look like garbage" symptom.
            // Re-applying here, on the real SIZE_CHANGED event that fires
            // once the backing surface has actually changed, is correct
            // regardless of that timing. Also re-arms each window's
            // redraw-on-change gate, since none of those keys factor in
            // size/scale (see applyUiScale's own reset of the same).
            if (ev.window.windowID == mainWindowID) {
                ApplyHiDpiRenderScale(renderer, app::layout::kWindowW, app::layout::kWindowH);
            } else if (ev.window.windowID == eqWindowID) {
                ApplyHiDpiRenderScale(eqRenderer, app::layout::kEqWindowW, app::layout::kEqWindowH);
                lastEqKey.reset();
            } else if (ev.window.windowID == plWindowID) {
                ApplyHiDpiRenderScale(plRenderer, app::layout::kPlaylistWindowW, app::layout::kPlaylistWindowH);
                lastPlKey.reset();
            } else if (ev.window.windowID == infoWindowID) {
                ApplyHiDpiRenderScale(infoRenderer, app::layout::kInfoWindowW, app::layout::kInfoWindowH);
                lastInfoKey.reset();
            } else if (ev.window.windowID == aboutWindowID) {
                ApplyHiDpiRenderScale(aboutRenderer, app::layout::kAboutWindowW, app::layout::kAboutWindowH);
                aboutDrawnOnce = false;
            }
        }
#ifdef __APPLE__
        // Only reachable if something other than the minimize button
        // triggered a real OS miniaturize - the button itself calls
        // enterAppTray() directly (see its click handler), so the normal
        // path never lets any window genuinely miniaturize at all, and
        // never posts real SDL_WINDOWEVENT_RESTORED/FOCUS_GAINED either
        // (restoring is driven entirely by kTrayRestoreEventType below,
        // not by this block). Converts to the same tray-hidden state
        // either way, then immediately un-miniaturizes whichever window
        // actually got miniaturized (same no-Dock-tile reasoning as
        // enterAppTray's own comment).
        if (ev.type == SDL_WINDOWEVENT && ev.window.event == SDL_WINDOWEVENT_MINIMIZED) {
            enterAppTray();
            SDL_Window* w = SDL_GetWindowFromID(ev.window.windowID);
            if (w) SDL_HideWindow(w);
        }
        // Deliberately a dedicated custom event, not
        // SDL_WINDOWEVENT_RESTORED/FOCUS_GAINED: an earlier version
        // reacted to those instead, but hiding Main + switching the
        // app's activation policy in enterAppTray() turned out to itself
        // generate a spurious event of that same shape for Main
        // moments later, indistinguishable from a real menu-bar click,
        // which made the app immediately undo its own minimize. See
        // ShowMenuBarIcon's click handler (menu_bar_icon.mm) - it's the
        // only thing that ever posts this event type.
        if (ev.type == kTrayRestoreEventType) {
            exitAppTray();
        }
        // Posted by the tray popup's "About" item (menu_bar_icon.mm).
        // Deliberately does NOT call exitAppTray() - shows only the
        // About window, leaving Main/EQ/Playlist/Info and the tray icon
        // exactly as they were; unconditional (not toggleSecondaryWindow)
        // since aboutUserVisible can already be true here (it isn't
        // cleared by enterAppTray(), only the real window gets hidden),
        // and this item should always end up showing it, never hiding it.
        if (ev.type == kTrayAboutEventType) {
            app::ActivateApp();
            aboutUserVisible = true;
            aboutMinimized = false;
            SDL_ShowWindow(aboutWindow);
            SDL_RaiseWindow(aboutWindow);
        }
        // Posted by media_remote.mm's MPRemoteCommandCenter handlers -
        // Bluetooth headphones/AirPods/car-stereo play-pause-next-previous
        // presses. Previous maps to Back, matching the Play/Stop/Next/
        // Pause/Back/Eject vocabulary handleTransportPress already speaks.
        if (ev.type == kMediaRemoteEventType) {
            switch (static_cast<app::MediaRemoteCommand>(ev.user.code)) {
                case app::MediaRemoteCommand::Play: handleTransportPress(TransportAction::Play); break;
                case app::MediaRemoteCommand::Pause: handleTransportPress(TransportAction::Pause); break;
                case app::MediaRemoteCommand::Next: handleTransportPress(TransportAction::Next); break;
                case app::MediaRemoteCommand::Previous: handleTransportPress(TransportAction::Back); break;
            }
        }
        // TODO: "Add a setting for UI scale / text+button size" - posted
        // by main_menu.mm's View-menu items and their Cmd+/-/0 key
        // equivalents.
        if (ev.type == kUiScaleEventType) {
            constexpr int kMaxIdx = static_cast<int>(std::size(kUiScaleLevels)) - 1;
            int idx = 0;
            for (int i = 0; i <= kMaxIdx; ++i) {
                if (kUiScaleLevels[i] == uiScalePercent) idx = i;
            }
            switch (static_cast<app::UiScaleMenuAction>(ev.user.code)) {
                case app::UiScaleMenuAction::Set100: applyUiScale(100); break;
                case app::UiScaleMenuAction::Set125: applyUiScale(125); break;
                case app::UiScaleMenuAction::Set150: applyUiScale(150); break;
                case app::UiScaleMenuAction::ZoomIn: applyUiScale(kUiScaleLevels[std::min(idx + 1, kMaxIdx)]); break;
                case app::UiScaleMenuAction::ZoomOut: applyUiScale(kUiScaleLevels[std::max(idx - 1, 0)]); break;
                case app::UiScaleMenuAction::Reset: applyUiScale(100); break;
            }
        }
        // Posted by main_menu.mm's Effects-menu items.
        if (ev.type == kEffectsEventType) {
            switch (static_cast<app::EffectsMenuAction>(ev.user.code)) {
                case app::EffectsMenuAction::ToggleXSound: toggleXSound(); break;
                case app::EffectsMenuAction::ToggleReverb: toggleReverb(); break;
                case app::EffectsMenuAction::ToggleSaturation: toggleSaturation(); break;
                case app::EffectsMenuAction::ToggleCompression: toggleCompression(); break;
                case app::EffectsMenuAction::ToggleChorus: toggleChorus(); break;
            }
        }
#else
        if (ev.type == SDL_WINDOWEVENT && (ev.window.event == SDL_WINDOWEVENT_MINIMIZED ||
                                            ev.window.event == SDL_WINDOWEVENT_RESTORED ||
                                            ev.window.event == SDL_WINDOWEVENT_FOCUS_GAINED)) {
            // TODO: "when the main window is minimized or maximized all
            // the windows should minimize or maximize". Symmetric across
            // all four windows, not just Main: each is a genuinely
            // separate top-level window, so on macOS each gets its own
            // Dock icon once minimized, and restoring *any one* of those
            // icons must bring the rest back too.
            //
            // Two real-machine-only bugs already found and fixed here:
            // (1) an earlier version only listened on Main, so restoring
            // via any other window's Dock icon restored just that one.
            // (2) after fixing that, restoring via a *secondary* window's
            // Dock icon still didn't cascade - SDL_WINDOWEVENT_RESTORED
            // was only ever observed to actually arrive for Main; testing
            // also separately confirmed SDL_MinimizeWindow sets
            // SDL_WINDOW_HIDDEN alongside SDL_WINDOW_MINIMIZED on this
            // platform, ruling out that flag as a way to detect "still
            // minimized" too. So this now also reacts to
            // SDL_WINDOWEVENT_FOCUS_GAINED - reliably delivered by any
            // Dock-icon click - gated on our *own* minimized-bool
            // bookkeeping (mainMinimized/eqMinimized/plMinimized/
            // infoMinimized, set only by this same code, never read from
            // an SDL flag) rather than SDL's state, so an ordinary click
            // on an already-visible window can't misfire this.
            //
            // userVisible gates which windows are even eligible - a
            // window the user closed on purpose stays closed regardless
            // of what Main or anything else does. Main has no user-hide
            // concept (only Close, which quits), so it's always eligible.
            // "Maximize" (the repurposed LitePic triangle - see
            // toggleMaximize) has its own independent show/hide logic and
            // isn't part of this cascade.
            // TODO: "when the main window is minimized or maximized all
            // the windows should minimize or maximize". Symmetric across
            // all four windows, not just Main: each is a genuinely
            // separate top-level window, so on macOS each gets its own
            // Dock icon once minimized, and restoring *any one* of those
            // icons must bring the rest back too.
            //
            // Two real-machine-only bugs already found and fixed here:
            // (1) an earlier version only listened on Main, so restoring
            // via any other window's Dock icon restored just that one.
            // (2) after fixing that, restoring via a *secondary* window's
            // Dock icon still didn't cascade - SDL_WINDOWEVENT_RESTORED
            // was only ever observed to actually arrive for Main; testing
            // also separately confirmed SDL_MinimizeWindow sets
            // SDL_WINDOW_HIDDEN alongside SDL_WINDOW_MINIMIZED on this
            // platform, ruling out that flag as a way to detect "still
            // minimized" too. So this now also reacts to
            // SDL_WINDOWEVENT_FOCUS_GAINED - reliably delivered by any
            // Dock-icon click - gated on our *own* minimized-bool
            // bookkeeping (mainMinimized/eqMinimized/plMinimized/
            // infoMinimized, set only by this same code, never read from
            // an SDL flag) rather than SDL's state, so an ordinary click
            // on an already-visible window can't misfire this.
            //
            // userVisible gates which windows are even eligible - a
            // window the user closed on purpose stays closed regardless
            // of what Main or anything else does. Main has no user-hide
            // concept (only Close, which quits), so it's always eligible.
            // "Maximize" (the repurposed LitePic triangle - see
            // toggleMaximize) has its own independent show/hide logic and
            // isn't part of this cascade.
            struct Target {
                SDL_Window* win;
                bool* minimized;
                bool userVisible;
            };
            Target mainT{window, &mainMinimized, true};
            Target eqT{eqWindow, &eqMinimized, eqUserVisible};
            Target plT{plWindow, &plMinimized, plUserVisible};
            Target infoT{infoWindow, &infoMinimized, infoUserVisible};
            Target aboutT{aboutWindow, &aboutMinimized, aboutUserVisible};
            Target* all[] = {&mainT, &eqT, &plT, &infoT, &aboutT};
            Target* triggered = nullptr;
            for (Target* t : all) {
                if (SDL_GetWindowID(t->win) == ev.window.windowID) triggered = t;
            }
            if (triggered) {
                if (ev.window.event == SDL_WINDOWEVENT_MINIMIZED) {
                    *triggered->minimized = true;
                    for (Target* t : all) {
                        if (t == triggered || !t->userVisible) continue;
                        *t->minimized = true;
                        SDL_MinimizeWindow(t->win);
                    }
                } else if (*triggered->minimized) {
                    // RESTORED or FOCUS_GAINED, and we believed this
                    // window was minimized - a real "coming back" event,
                    // not just an ordinary click/focus.
                    *triggered->minimized = false;
                    // Fixed restore order (Info, Playlist, EQ, then Main
                    // last) - the user found the resulting stacking
                    // distracting when this just followed struct-
                    // declaration order. Main is unconditionally
                    // restored+raised last regardless of which window
                    // actually triggered the cascade (harmless no-op if
                    // it's already frontmost, e.g. when Main itself was
                    // the trigger), so it always ends up on top rather
                    // than wherever the clicked Dock icon happened to
                    // leave it.
                    Target* restoreOrder[] = {&aboutT, &infoT, &plT, &eqT};
                    for (Target* t : restoreOrder) {
                        if (t == triggered || !t->userVisible || !*t->minimized) continue;
                        *t->minimized = false;
                        SDL_RestoreWindow(t->win);
                    }
                    mainMinimized = false;
                    SDL_RestoreWindow(window);
                    SDL_RaiseWindow(window);
                }
            }
        }
#endif

        if (ev.type == SDL_MOUSEBUTTONDOWN && ev.button.button == SDL_BUTTON_LEFT) {
            // Raises all three windows together, clicked one on top. Without
            // this, clicking Main doesn't reliably bring EQ/Playlist forward
            // with it - only whichever window macOS happened to have
            // ordered above the others already comes along, which reads as
            // "EQ doesn't raise but Playlist does" depending on creation/
            // focus history. Docked windows should always move as one unit.
            SDL_Window* clicked = ev.button.windowID == mainWindowID    ? window
                                   : ev.button.windowID == plWindowID   ? plWindow
                                   : ev.button.windowID == eqWindowID   ? eqWindow
                                   : ev.button.windowID == infoWindowID ? infoWindow
                                   : ev.button.windowID == aboutWindowID ? aboutWindow
                                                                         : nullptr;
            if (clicked) {
                // Info/About aren't part of the required docking group, but
                // still get raised along with a click on any of the other
                // windows - otherwise clicking Main while one of them
                // happens to be on top would leave it obscuring Main.
                for (SDL_Window* w : {window, eqWindow, plWindow, infoWindow, aboutWindow}) {
                    if (w != clicked && !(SDL_GetWindowFlags(w) & SDL_WINDOW_HIDDEN)) SDL_RaiseWindow(w);
                }
                SDL_RaiseWindow(clicked);
            }

            const int lx = static_cast<int>(ev.button.x / scale), ly = static_cast<int>(ev.button.y / scale);
            if (ev.button.windowID == mainWindowID) {
#ifndef __APPLE__
                // Linux twins of the macOS View/Effects NSMenu items - same
                // "any click while open closes it, click-inside dispatches"
                // convention as ejectMenuOpen just below, checked first for
                // the same reason (a popup overlapping other controls must
                // intercept every click while open, never fall through).
                if (viewMenuOpen) {
                    viewMenuOpen = false;
                    if (inRect(lx, ly, kViewMenuX, kViewMenuY, kViewMenuW, kViewMenuH)) {
                        constexpr int kMaxIdx = static_cast<int>(std::size(kUiScaleLevels)) - 1;
                        int curIdx = 0;
                        for (int i = 0; i <= kMaxIdx; ++i) {
                            if (kUiScaleLevels[i] == uiScalePercent) curIdx = i;
                        }
                        const int item = (ly - kViewMenuY) / kViewMenuItemH;
                        switch (item) {
                            case 0: applyUiScale(100); break;
                            case 1: applyUiScale(200); break;
                            case 2: applyUiScale(300); break;
                            case 3: applyUiScale(400); break;
                            case 4: applyUiScale(kUiScaleLevels[std::min(curIdx + 1, kMaxIdx)]); break; // Zoom In
                            case 5: applyUiScale(kUiScaleLevels[std::max(curIdx - 1, 0)]); break;       // Zoom Out
                            case 6: applyUiScale(100); break;                                           // Reset
                        }
                    }
                } else if (effectsMenuOpen) {
                    effectsMenuOpen = false;
                    if (inRect(lx, ly, kEffectsMenuX, kEffectsMenuY, kEffectsMenuW, kEffectsMenuH)) {
                        const int item = (ly - kEffectsMenuY) / kEffectsMenuItemH;
                        switch (item) {
                            case 0: toggleXSound(); break;
                            case 1: toggleReverb(); break;
                            case 2: toggleSaturation(); break;
                            case 3: toggleCompression(); break;
                            case 4: toggleChorus(); break;
                        }
                    }
                } else if (inRect(lx, ly, kViewToggleX, kViewToggleY, kViewToggleW, kViewToggleH)) {
                    viewMenuOpen = true;
                    effectsMenuOpen = false;
                    ejectMenuOpen = false;
                } else if (inRect(lx, ly, kEffectsToggleX, kEffectsToggleY, kEffectsToggleW, kEffectsToggleH)) {
                    effectsMenuOpen = true;
                    viewMenuOpen = false;
                    ejectMenuOpen = false;
                } else
#endif
                if (ejectMenuOpen) {
                    // Mirrors the Playlist window's Save-menu pattern: any
                    // click while open closes it, whether or not it landed
                    // on an item - never falls through to other buttons/
                    // controls underneath, same as there.
                    ejectMenuOpen = false;
                    if (inRect(lx, ly, kEjectMenuX, kEjectMenuY, kEjectMenuW, kEjectMenuH)) {
                        const int item = (ly - kEjectMenuY) / kEjectMenuItemH;
                        if (item == 0) onAddFilesRequested();
                        else if (item == 1) onAddFolderRequested();
                    }
                } else if (inRect(lx, ly, kMinimizeX, kMinimizeY, 10, 9)) {
#ifdef __APPLE__
                    enterAppTray();
#else
                    SDL_MinimizeWindow(window);
#endif
                } else if (inRect(lx, ly, kCloseX, kCloseY, kCloseSize, kCloseSize)) {
                    running = false;
                } else if (inRect(lx, ly, kLiteX, kLiteY, 10, 9)) {
                    toggleMaximize();
                } else if (inRect(lx, ly, kShockX, kShockY, 16, 16)) {
                    // The LOGOJAP/"shock" icon - in the original just
                    // retriggered a random title-scroll effect
                    // (LowHigh_Click), not ported here either; this TODO
                    // repurposes the same click to open About instead.
                    toggleSecondaryWindow(aboutWindow, aboutUserVisible, aboutMinimized);
                } else if (inRect(lx, ly, kAnalyzerX, kAnalyzerY, kAnalyzerW, kAnalyzerH)) {
                    // Matches the original's fixed 3-way click cycle
                    // (analyzer_Click -> ImgLogo_Click -> picCardioSin/
                    // Des_Click -> analyzer again), just collapsed into one
                    // handler since all three targets ever do is advance to
                    // the next panel in the same fixed order.
                    visPanel = static_cast<VisPanel>((static_cast<int>(visPanel) + 1) % 3);
                } else if (inRect(lx, ly, kPlToggleX, kPlToggleY, kPlToggleW, kPlToggleH)) {
                    toggleSecondaryWindow(plWindow, plUserVisible, plMinimized);
                } else if (inRect(lx, ly, kEqToggleX, kEqToggleY, kEqToggleW, kEqToggleH)) {
                    toggleSecondaryWindow(eqWindow, eqUserVisible, eqMinimized);
                } else {
                    bool hit = false;
                    for (size_t i = 0; i < transportButtons.size(); ++i) {
                        const SDL_Rect& r = transportButtons[i].rect;
                        if (lx >= r.x && lx < r.x + r.w && ly >= r.y && ly < r.y + r.h) {
                            pressedButton = static_cast<int>(i);
                            handleTransportPress(transportButtons[i].action);
                            hit = true;
                            break;
                        }
                    }
                    if (!hit) {
                        for (size_t i = 0; i < utilityButtons.size(); ++i) {
                            const SDL_Rect& r = utilityButtons[i].rect;
                            if (lx >= r.x && lx < r.x + r.w && ly >= r.y && ly < r.y + r.h) {
                                pressedUtility = static_cast<int>(i);
                                handleUtilityPress(utilityButtons[i].action);
                                hit = true;
                                break;
                            }
                        }
                    }
                    if (!hit && lx >= kVolSliderX && lx < kVolSliderX + kVolSliderW && ly >= kVolSliderY &&
                        ly < kVolSliderY + kVolSliderH) {
                        const int trackTop = kVolSliderY + kVolArrowSize;
                        const int trackBottom = kVolSliderY + kVolSliderH - kVolArrowSize;
                        if (ly < trackTop) {
                            engine.SetVolume(engine.Volume() + kVolStep);
                            engine.SetMuted(false);
                        } else if (ly >= trackBottom) {
                            engine.SetVolume(engine.Volume() - kVolStep);
                            engine.SetMuted(false);
                        } else {
                            volDragging = true;
                            handleVolSliderClickAt(ly);
                        }
                        hit = true;
                    }
                    if (!hit && lx >= kSeekX0 && lx < kSeekX1 && ly >= kSeekY - 4 && ly < kSeekY + 4) {
                        seekDragging = true;
                        handleSeekBarClickAt(lx);
                        hit = true;
                    }
                    if (!hit && ly < kDragStripH) beginDrag(mainDrag);
                }
            } else if (ev.button.windowID == plWindowID) {
                if (inRect(lx, ly, kCloseX, kCloseY, kCloseSize, kCloseSize)) {
                    plUserVisible = false;
                    SDL_HideWindow(plWindow);
                } else if (ly < kDragStripH) {
                    beginDrag(plDrag);
                } else {
                    handlePlaylistClickAt(lx, ly);
                }
            } else if (ev.button.windowID == eqWindowID) {
                if (inRect(lx, ly, kCloseX, kCloseY, kCloseSize, kCloseSize)) {
                    eqUserVisible = false;
                    SDL_HideWindow(eqWindow);
                } else if (ly < kDragStripH) {
                    beginDrag(eqDrag);
                } else {
                    handleEqClickAt(lx, ly);
                }
            } else if (ev.button.windowID == infoWindowID) {
                if (inRect(lx, ly, kCloseX, kCloseY, kCloseSize, kCloseSize)) {
                    infoUserVisible = false;
                    SDL_HideWindow(infoWindow);
                } else if (ly < kDragStripH) {
                    beginDrag(infoDrag);
                }
            } else if (ev.button.windowID == aboutWindowID) {
                if (inRect(lx, ly, kCloseX, kCloseY, kCloseSize, kCloseSize)) {
                    aboutUserVisible = false;
                    SDL_HideWindow(aboutWindow);
                } else if (ly < kDragStripH) {
                    beginDrag(aboutDrag);
                }
            }
        } else if (ev.type == SDL_MOUSEBUTTONUP && ev.button.button == SDL_BUTTON_LEFT) {
            pressedButton = -1;
            pressedUtility = -1;
            plPressedButton = -1;
            eqPressedSlider = -1;
            eqPressedPreset = -1;
            volDragging = false;
            seekDragging = false;
            mainDrag.active = plDrag.active = eqDrag.active = infoDrag.active = aboutDrag.active = false;
        } else if (ev.type == SDL_MOUSEMOTION) {
            // Dragging itself is driven per-frame below, not from here - a
            // fast drag can outrun the window entirely (cursor ends up over
            // another xmad window, a gap between them, or the desktop), at
            // which point no more SDL_MOUSEMOTION events for any xmad
            // window arrive at all, and a motion-event-gated update would
            // leave the drag frozen mid-air even though the window itself
            // never got a mouse-up.
            if (volDragging && ev.motion.windowID == mainWindowID) {
                handleVolSliderClickAt(static_cast<int>(ev.motion.y / scale));
            }
            if (seekDragging && ev.motion.windowID == mainWindowID) {
                handleSeekBarClickAt(static_cast<int>(ev.motion.x / scale));
            }
            if (ev.motion.windowID == mainWindowID) {
                mainMouseX = static_cast<int>(ev.motion.x / scale);
                mainMouseY = static_cast<int>(ev.motion.y / scale);
            }
        } else if (ev.type == SDL_WINDOWEVENT && ev.window.event == SDL_WINDOWEVENT_LEAVE &&
                   ev.window.windowID == mainWindowID) {
            mainMouseX = mainMouseY = -1; // stop showing a stale tooltip once the cursor leaves
        } else if (ev.type == SDL_MOUSEWHEEL && ev.wheel.windowID == plWindowID) {
            plScrollOffset -= ev.wheel.y;
            const int maxOffset = std::max(0, static_cast<int>(playlist.size()) - kPlVisibleRows);
            plScrollOffset = std::clamp(plScrollOffset, 0, maxOffset);
        } else if (ev.type == SDL_DROPFILE) {
            // SDL owns this string (SDL_malloc'd) - must free it.
            handleDroppedFile(ev.drop.file);
            SDL_free(const_cast<char*>(ev.drop.file));
        }
    };

    if (testMinimizeCascade) {
        // Debug hook: exercises the exact same processEvent branch real
        // SDL_WINDOWEVENT_MINIMIZED/RESTORED/FOCUS_GAINED delivery would,
        // confirming the cascade logic itself picks the right windows
        // under the right condition. Reports our *own*
        // mainMinimized/eq/pl/infoMinimized bookkeeping, not
        // SDL_GetWindowFlags' SDL_WINDOW_MINIMIZED bit - this sandbox has
        // no real display attached, and that bit measurably reads back 0
        // here even when SDL_MinimizeWindow was genuinely called, so it's
        // not a usable signal in this environment. What this still can't
        // confirm headlessly: whether the real OS/WM actually delivers
        // these events at all for a real Minimize-button click or Dock
        // icon click - that needs the user's real machine (real-machine
        // testing is in fact what found bug (2) below).
        eqUserVisible = plUserVisible = infoUserVisible = true;
        mainMinimized = eqMinimized = plMinimized = infoMinimized = false;
        SDL_ShowWindow(eqWindow);
        SDL_ShowWindow(plWindow);
        SDL_ShowWindow(infoWindow);
        SDL_PumpEvents(); // let window state settle before the cascade runs below

        auto fire = [&](SDL_Window* w, Uint32 windowEvent) {
            SDL_Event ev{};
            ev.type = SDL_WINDOWEVENT;
            ev.window.event = static_cast<Uint8>(windowEvent);
            ev.window.windowID = SDL_GetWindowID(w);
            processEvent(ev);
        };
        auto report = [&](const char* label) {
            std::cout << label << ": mainMinimized=" << mainMinimized << " eqMinimized=" << eqMinimized
                      << " plMinimized=" << plMinimized << " infoMinimized=" << infoMinimized << "\n";
        };

        fire(window, SDL_WINDOWEVENT_MINIMIZED);
        report("after Main MINIMIZED");

        // Restoring from a *different* window than the one that triggered
        // the cascade (EQ, not Main) via RESTORED - the scenario the user
        // first asked about.
        fire(eqWindow, SDL_WINDOWEVENT_RESTORED);
        report("after EQ RESTORED");

        // Bug (2), found on the user's real machine: RESTORED alone
        // wasn't enough - only Main was ever observed to actually receive
        // it. Re-minimize everything, then restore via FOCUS_GAINED on a
        // *different* secondary window (Info) instead, which is the path
        // real Dock-icon clicks were confirmed to reliably deliver.
        fire(window, SDL_WINDOWEVENT_MINIMIZED);
        report("after Main MINIMIZED (again)");
        fire(infoWindow, SDL_WINDOWEVENT_FOCUS_GAINED);
        report("after Info FOCUS_GAINED");
    }

    if (simClickSpecModeCount > 0) {
        // Pushes REAL SDL mouse events at the SpecMode button's actual
        // screen coordinates and drains them through the exact same
        // processEvent the interactive loop uses - unlike --specmode-clicks
        // above, this exercises SDL's own coordinate/window-ID plumbing and
        // the rect hit-test, not just the handler lambda directly.
        const int cx = ScaledDim(kSpecModeX + kTransportBtnW / 2, scale);
        const int cy = ScaledDim(kSpecModeY + kTransportBtnH / 2, scale);
        for (int i = 0; i < simClickSpecModeCount; ++i) {
            SDL_Event down{};
            down.type = SDL_MOUSEBUTTONDOWN;
            down.button.button = SDL_BUTTON_LEFT;
            down.button.windowID = mainWindowID;
            down.button.x = cx;
            down.button.y = cy;
            processEvent(down);

            SDL_Event up{};
            up.type = SDL_MOUSEBUTTONUP;
            up.button.button = SDL_BUTTON_LEFT;
            up.button.windowID = mainWindowID;
            up.button.x = cx;
            up.button.y = cy;
            processEvent(up);
        }
        std::cout << "visMode after " << simClickSpecModeCount << " simulated real click(s) at (" << cx << ","
                  << cy << "): " << static_cast<int>(visMode) << "\n";
    }

    for (const auto& click : simClicks) {
        const std::string& win = click[0];
        const int lx = std::atoi(click[1].c_str());
        const int ly = std::atoi(click[2].c_str());
        Uint32 targetID = mainWindowID;
        if (win == "pl") targetID = plWindowID;
        else if (win == "eq") targetID = eqWindowID;
        else if (win != "main") {
            std::cerr << "unknown --sim-click window '" << win << "' (want main/pl/eq)\n";
            return 1;
        }

        SDL_Event down{};
        down.type = SDL_MOUSEBUTTONDOWN;
        down.button.button = SDL_BUTTON_LEFT;
        down.button.windowID = targetID;
        down.button.x = ScaledDim(lx, scale);
        down.button.y = ScaledDim(ly, scale);
        processEvent(down);

        SDL_Event up{};
        up.type = SDL_MOUSEBUTTONUP;
        up.button.button = SDL_BUTTON_LEFT;
        up.button.windowID = targetID;
        up.button.x = ScaledDim(lx, scale);
        up.button.y = ScaledDim(ly, scale);
        processEvent(up);

        int px, py, ex, ey;
        SDL_GetWindowPosition(plWindow, &px, &py);
        SDL_GetWindowPosition(eqWindow, &ex, &ey);
        std::cout << "sim-click " << win << " (" << lx << "," << ly << "): running=" << running
                  << " plHidden=" << static_cast<bool>(SDL_GetWindowFlags(plWindow) & SDL_WINDOW_HIDDEN)
                  << " eqHidden=" << static_cast<bool>(SDL_GetWindowFlags(eqWindow) & SDL_WINDOW_HIDDEN)
                  << " aboutHidden=" << static_cast<bool>(SDL_GetWindowFlags(aboutWindow) & SDL_WINDOW_HIDDEN)
                  << " visPanel=" << static_cast<int>(visPanel) << " visMode=" << static_cast<int>(visMode)
                  << " plPos=(" << px << "," << py << ") eqPos=(" << ex << "," << ey << ")"
                  << " positionSeconds=" << engine.positionSeconds() << " plSaveMenuOpen=" << plSaveMenuOpen
                  << " plClearConfirmOpen=" << plClearConfirmOpen << "\n";
    }

    if (hoverX >= 0 && hoverY >= 0) {
        // Debug hook: a real SDL_MOUSEMOTION through the exact same
        // processEvent path a genuine mouse move takes, so the tooltip
        // hit-test in drawFrame can be verified against real coordinates
        // rather than by poking mainMouseX/Y directly.
        SDL_Event motion{};
        motion.type = SDL_MOUSEMOTION;
        motion.motion.windowID = mainWindowID;
        motion.motion.x = ScaledDim(hoverX, scale);
        motion.motion.y = ScaledDim(hoverY, scale);
        processEvent(motion);
        std::cout << "hover (" << hoverX << "," << hoverY << "): mainMouseX=" << mainMouseX
                  << " mainMouseY=" << mainMouseY << "\n";
    }

    if (!dumpFramePath.empty() || !dumpPlaylistFramePath.empty() || !dumpEqFramePath.empty() ||
        !dumpInfoFramePath.empty() || !dumpAboutFramePath.empty()) {
        // Headless verification path: renders and reads pixels back via
        // SDL_RenderReadPixels, which reads SDL's own render target - no
        // screen-recording/OS permission required, unlike screencapture.
        bool ok = true;
        if (!dumpFramePath.empty()) {
            // The audio device runs on its own OS thread independent of
            // this render loop (Open() already called Play()), but needs a
            // moment to actually produce a callback and populate the vis
            // snapshot - without this, a --dump-frame taken immediately
            // after Open() would show empty/silent bars even though audio
            // has started.
            if (engine.channels() != 0) SDL_Delay(150);
            drawFrame(); // one real frame first, so bar/peak state actually reflects live audio
            if (visPauseFirst) engine.Pause();
            for (int i = 0; i < visTicks; ++i) drawFrame(); // step the fall/decay animation without presenting
            SDL_RenderPresent(renderer);
            ok &= dumpRenderTarget(renderer, ScaledDim(kWindowW, scale), ScaledDim(kWindowH, scale), dumpFramePath);
        }
        if (!dumpPlaylistFramePath.empty()) {
            drawPlaylistFrame();
            SDL_RenderPresent(plRenderer);
            ok &= dumpRenderTarget(plRenderer, ScaledDim(kPlaylistWindowW, scale), ScaledDim(kPlaylistWindowH, scale),
                                    dumpPlaylistFramePath);
        }
        if (!dumpEqFramePath.empty()) {
            drawEqFrame();
            SDL_RenderPresent(eqRenderer);
            ok &= dumpRenderTarget(eqRenderer, ScaledDim(kEqWindowW, scale), ScaledDim(kEqWindowH, scale),
                                    dumpEqFramePath);
        }
        if (!dumpInfoFramePath.empty()) {
            drawInfoFrame();
            SDL_RenderPresent(infoRenderer);
            ok &= dumpRenderTarget(infoRenderer, ScaledDim(kInfoWindowW, scale), ScaledDim(kInfoWindowH, scale),
                                    dumpInfoFramePath);
        }
        if (!dumpAboutFramePath.empty()) {
            drawAboutFrame();
            SDL_RenderPresent(aboutRenderer);
            ok &= dumpRenderTarget(aboutRenderer, ScaledDim(kAboutWindowW, scale), ScaledDim(kAboutWindowH, scale),
                                    dumpAboutFramePath);
        }

        SDL_DestroyRenderer(aboutRenderer);
        SDL_DestroyWindow(aboutWindow);
        SDL_DestroyRenderer(infoRenderer);
        SDL_DestroyWindow(infoWindow);
        SDL_DestroyRenderer(eqRenderer);
        SDL_DestroyWindow(eqWindow);
        SDL_DestroyRenderer(plRenderer);
        SDL_DestroyWindow(plWindow);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return ok ? 0 : 1;
    }

    // --auto-advance-test: exercises the exact loop below (not a
    // reimplementation) for a bounded wall-clock duration, logging every
    // currentIndex change - lets auto-advance-on-natural-finish be
    // verified headlessly against real decode/playback timing rather than
    // just asserted.
    const Uint32 autoAdvanceDeadline =
        autoAdvanceTestSeconds > 0.0 ? SDL_GetTicks() + static_cast<Uint32>(autoAdvanceTestSeconds * 1000) : 0;
    int lastLoggedIndex = playlist.currentIndex();
    if (autoAdvanceDeadline != 0) {
        std::cout << "auto-advance-test: start index=" << lastLoggedIndex << "\n";
    }

    audio::PlayState lastEngineState = engine.state();
    // Now Playing info (Control Center/lock screen/Bluetooth remote
    // status) is otherwise only pushed on explicit state-change events
    // (see notifyNowPlayingChanged's call sites) - a periodic refresh on
    // top of that guards against macOS treating a long-untouched app as
    // stale and handing "current Now Playing app" status to something
    // else, which is what actually gates whether Bluetooth commands
    // route here at all. No-ops harmlessly on non-Darwin, where
    // notifyNowPlayingChanged is permanently the default do-nothing
    // lambda.
    Uint32 lastNowPlayingRefresh = 0;
    while (running && (autoAdvanceDeadline == 0 || SDL_GetTicks() < autoAdvanceDeadline)) {
        const Uint32 frameStart = SDL_GetTicks();

        SDL_Event ev;
        while (SDL_PollEvent(&ev)) processEvent(ev);

#ifdef __APPLE__
        // SDL's Cocoa backend pumps only the specific event mask it asks
        // for (SDL_PumpEvents -> nextEventMatchingMask), not the full
        // default-mode CFRunLoop - callbacks delivered via CoreFoundation
        // run-loop sources never fire without this, which is how
        // MPRemoteCommandCenter's command blocks actually arrive from the
        // system's mediaremoted (registering a command succeeds either
        // way; only the callback delivery itself was silently starved).
        // Non-blocking: returns immediately once a pending source is
        // serviced, or right away if none is. Same technique/parameters
        // (bar the timeout) a real SDL project (MAME's SDL3 backend)
        // needed for GCController's async device discovery, for the same
        // underlying reason - see libsdl-org/SDL#11742.
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0, true);
#endif

        // Polled every frame (not just on SDL_MOUSEMOTION) so a fast drag
        // that outruns the window - leaving it stranded outside every xmad
        // window's bounds, where no motion events arrive for any of them -
        // still keeps tracking the live cursor position each frame instead
        // of freezing mid-drag. Each call already no-ops when its
        // DragState isn't active.
        updateDrag(mainDrag);
        updateDrag(plDrag);
        updateDrag(eqDrag);
        updateDrag(infoDrag);
        updateDrag(aboutDrag);

        // Auto-advance: engine.state() can drop to Stopped either because
        // the user pressed Stop, or because playback reached the end of
        // the track (see engine.h's decoderExhausted_ comment - state_
        // only flips once everything decoded has actually reached the
        // speakers). userStoppedTrack disambiguates the two; only a
        // natural end-of-track should pull in the next playlist entry.
        const audio::PlayState curEngineState = engine.state();
        if (lastEngineState == audio::PlayState::Playing && curEngineState == audio::PlayState::Stopped) {
            if (!userStoppedTrack && !playlist.empty()) {
                openPlaylistIndex(playlist.WrappedIndex(1));
            }
            userStoppedTrack = false;
        }
        lastEngineState = engine.state();

        if (!playlist.empty() && frameStart - lastNowPlayingRefresh >= 5000) {
            lastNowPlayingRefresh = frameStart;
            notifyNowPlayingChanged(curEngineState == audio::PlayState::Playing);
        }

        if (autoAdvanceDeadline != 0 && playlist.currentIndex() != lastLoggedIndex) {
            lastLoggedIndex = playlist.currentIndex();
            std::cout << "auto-advance-test: index -> " << lastLoggedIndex
                       << " at t=" << SDL_GetTicks() << "ms state=" << static_cast<int>(engine.state()) << "\n";
        }

        if (!mainMinimized) {
            drawFrame();
            SDL_RenderPresent(renderer);
        }
        if (plUserVisible && !plMinimized) {
            PlaylistFrameKey plKey{playlist.Generation(), playlist.currentIndex(), plSelected,
                                    plScrollOffset,        plPressedButton,        engine.sampleRate(),
                                    engine.channels(),     plSaveMenuOpen,         plClearConfirmOpen};
            if (!lastPlKey || !(*lastPlKey == plKey)) {
                drawPlaylistFrame();
                SDL_RenderPresent(plRenderer);
                lastPlKey = plKey;
            }
        }
        if (eqUserVisible && !eqMinimized) {
            EqFrameKey eqKey;
            for (int b = 0; b < audio::Equalizer::kBands; ++b) eqKey.bands[static_cast<size_t>(b)] = engine.EqBand(b);
            eqKey.pressedSlider = eqPressedSlider;
            eqKey.currentPreset = eqCurrentPreset;
            eqKey.perSongEq = perSongEqEnabled;
            if (!lastEqKey || !(*lastEqKey == eqKey)) {
                drawEqFrame();
                SDL_RenderPresent(eqRenderer);
                lastEqKey = eqKey;
            }
        }
        if (infoUserVisible && !infoMinimized) {
            InfoFrameKey infoKey{playlist.Generation(), playlist.currentIndex(), engine.channels(),
                                  engine.sampleRate(),   engine.durationSeconds()};
            if (!lastInfoKey || !(*lastInfoKey == infoKey)) {
                drawInfoFrame();
                SDL_RenderPresent(infoRenderer);
                lastInfoKey = infoKey;
            }
        }
        if (aboutUserVisible && !aboutMinimized && !aboutDrawnOnce) {
            // Purely static branding (see drawAboutFrame's own comment) -
            // never changes after the very first draw, so unlike the other
            // three windows this needs no per-frame key at all.
            drawAboutFrame();
            SDL_RenderPresent(aboutRenderer);
            aboutDrawnOnce = true;
        }

        // The loop above has no vsync/blocking wait to throttle it (all
        // renderers are SDL_RENDERER_SOFTWARE), so without this cap it
        // spins as fast as the CPU allows regardless of playback state -
        // pegging a full core at ~85% even at idle. 60fps keeps animation
        // (marquee, spectrum falls/decay, which are tuned in pixels-per-
        // redraw-tick, not delta-time) smooth while giving the CPU back
        // between frames.
        constexpr Uint32 kFrameBudgetMs = 1000 / 60;
        const Uint32 elapsed = SDL_GetTicks() - frameStart;
        if (elapsed < kFrameBudgetMs) SDL_Delay(kFrameBudgetMs - elapsed);
    }
    if (autoAdvanceDeadline != 0) {
        std::cout << "auto-advance-test: end index=" << playlist.currentIndex()
                   << " state=" << static_cast<int>(engine.state())
                   << " positionSeconds=" << engine.positionSeconds() << "\n";
    }

    // Session persistence, save half: mirrors the load gate near playlist
    // setup, but always runs on a normal exit regardless of how this
    // session started - matches the original's Form_Unload, which always
    // writes preferences on the way out (see app/session.h).
    {
        app::Settings toSave;
        toSave.specMode = static_cast<int>(visMode);
        toSave.volumePercent = static_cast<int>(std::lround(engine.Volume() * 100.0f));
        toSave.wasPlaying = engine.state() == audio::PlayState::Playing;
        toSave.wasPaused = engine.state() == audio::PlayState::Paused;
        toSave.currentIndex = std::max(0, playlist.currentIndex());
        toSave.positionSeconds = engine.positionSeconds();
        toSave.xSound = engine.XSound();
        toSave.reverb = engine.ReverbOn();
        toSave.saturation = engine.SaturationOn();
        toSave.compression = engine.CompressionOn();
        toSave.chorus = engine.ChorusOn();
        toSave.eqPreset = eqCurrentPreset;
        toSave.visPanel = static_cast<int>(visPanel);
        toSave.perSongEq = perSongEqEnabled;
        toSave.uiScalePercent = uiScalePercent;
        for (int b = 0; b < audio::Equalizer::kBands; ++b) {
            toSave.eqBands[static_cast<size_t>(b)] = engine.EqBand(b);
        }
        app::SaveSettingsFile(app::SettingsFilePath(), toSave);
        playlist.SaveM3U(app::SessionPlaylistPath());
        app::SaveEqPerSongFile(app::EqPerSongPath(), perSongEqBands);
    }

    SDL_DestroyRenderer(aboutRenderer);
    SDL_DestroyWindow(aboutWindow);
    SDL_DestroyRenderer(infoRenderer);
    SDL_DestroyWindow(infoWindow);
    SDL_DestroyRenderer(eqRenderer);
    SDL_DestroyWindow(eqWindow);
    SDL_DestroyRenderer(plRenderer);
    SDL_DestroyWindow(plWindow);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
