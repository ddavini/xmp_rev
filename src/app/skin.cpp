#include "app/skin.h"

namespace xmad::app {

Skin Skin::Load(const std::string& dir) {
    Skin s;
    s.btnBack = gfx::LoadBMP(dir + "/back.bmp");
    s.btnPlay = gfx::LoadBMP(dir + "/play.bmp");
    s.btnStop = gfx::LoadBMP(dir + "/stop.bmp");
    s.btnNext = gfx::LoadBMP(dir + "/next.bmp");
    s.btnPause = gfx::LoadBMP(dir + "/pause.bmp");
    s.btnEject = gfx::LoadBMP(dir + "/eject.bmp");
    s.btnInfo = gfx::LoadBMP(dir + "/info.bmp");
    s.btnVol = gfx::LoadBMP(dir + "/vol.bmp");
    s.btnMute = gfx::LoadBMP(dir + "/mute.bmp");
    s.btnSpecMode = gfx::LoadBMP(dir + "/specmode.bmp");
    s.logo = gfx::LoadBMP(dir + "/logo.bmp");
    s.lite = gfx::LoadBMP(dir + "/lite.bmp");
    s.shock = gfx::LoadBMP(dir + "/shock.bmp");
    s.minimize = gfx::LoadBMP(dir + "/minimize.bmp");
    s.aboutIcon = gfx::LoadBMP(dir + "/about_icon.bmp");
    s.aboutZLogo = gfx::LoadBMP(dir + "/about_zlogo.bmp");
    s.idleLogo = gfx::LoadBMP(dir + "/idle_logo.bmp");
    s.volUp = gfx::LoadBMP(dir + "/vol_up.bmp");
    s.volDown = gfx::LoadBMP(dir + "/vol_down.bmp");
    s.plClear = gfx::LoadBMP(dir + "/clear.bmp");
    s.plDelete = gfx::LoadBMP(dir + "/delete.bmp");
    s.plSave = gfx::LoadBMP(dir + "/save.bmp");
    s.plSeek = gfx::LoadBMP(dir + "/seek.bmp");
    s.modeNone = gfx::LoadBMP(dir + "/mode_none.bmp");
    s.modeMono = gfx::LoadBMP(dir + "/mode_mono.bmp");
    s.modeStereo = gfx::LoadBMP(dir + "/mode_stereo.bmp");
    s.modeXSound = gfx::LoadBMP(dir + "/mode_xsound.bmp");
    s.displayFont = gfx::LoadBMP(dir + "/display_font.bmp");
    return s;
}

} // namespace xmad::app
