#pragma once

// Main window layout, in native (1x) pixels. Every constant here is derived
// directly from Source/xmp.frm's control coordinates (twips / 15 = px at
// standard 96dpi) - see the transport row note below for the one place the
// .frm's design-time values are overridden by Form_Load's runtime reflow.

namespace xmad::app::layout {

constexpr int kWindowW = 330;
// The .frm's raw ClientHeight (4050 twips) left ~120px of genuinely dead
// black space between the transport row (ends y=117) and the peak meters
// (started at y=240) - shrunk to fit snugly around the compacted content
// below instead of carrying that gap forward literally.
constexpr int kWindowH = 155;

// Transport row (CommandImg indices 0-5). Design-time .frm positions are
// placeholders; Form_Load's `For I = 1 To 5: CommandImg(I).Left = ...`
// loop packs them contiguously left-to-right in *index* order once
// AutoSize snaps each to its 23x13 bitmap: Back, Play, Stop, Next, Pause,
// Eject - that's the order that ships, not the .frm's raw Left values.
constexpr int kTransportY = 104;
constexpr int kTransportBtnW = 23;
constexpr int kTransportBtnH = 13;
constexpr int kTransportX0 = 16;

// Eject's dropdown (TODO: "open all the .mp3s or .flacs [if] a directory
// is selected") - same two-item, no-hover-highlight style as the
// Playlist window's Save menu (kPlSaveMenu* below), just relocated to
// Main and anchored above the Eject button (index 5 in the transport
// row) rather than below it, opening upward into empty space above the
// transport row instead of over other controls.
constexpr int kEjectMenuW = 64, kEjectMenuItemH = 12;
constexpr int kEjectMenuH = kEjectMenuItemH * 2; // Add Files, Add Folder
constexpr int kEjectMenuX = kTransportX0 + 5 * kTransportBtnW;
constexpr int kEjectMenuY = kTransportY - kEjectMenuH;

// Utility toggles (CommandImg indices 6-9) keep their .frm design-time
// positions - Form_Load never reflows these.
// kShowVolX/Y: in the original this opened a floating LED volume readout
// window (frmVolume) - dropped as a separate window here, and the button
// itself went unwired. Repurposed at the user's explicit request as the
// "Full Potato Mode" toggle (see UtilityAction::FullPotato in main.cpp) -
// a deliberate behavior substitution on an existing control, not a new
// button, same as kLiteX's maximize repurposing above.
constexpr int kShowVolX = 143, kShowVolY = 72;
constexpr int kMuteX = 168, kMuteY = 72;
constexpr int kSpecModeX = 168, kSpecModeY = 88;
constexpr int kInfoX = 168, kInfoY = 104;

// "Engage Full Potato Mode?" confirmation popup for the button above -
// there's no persistent on-screen indicator that Potato mode is active
// (only the Options>Potato menu's checkmarks), so this warns before
// turning it on rather than silently flipping both flags. Same
// label+YES+NO dropdown-overlay shape as kPlClearConfirm* (Playlist
// window), just anchored to kShowVolX/Y instead. Opens downward - unlike
// kEjectMenu*/kOptionsMenuTop* above it, there's clear room below
// kShowVolY within the 155px-tall main window.
constexpr int kPotatoConfirmItems = 3; // label + YES + NO
constexpr int kPotatoConfirmItemH = 12;
constexpr int kPotatoConfirmW = 72; // fits "FULL POTATO?" (12 chars * 5px + margin)
constexpr int kPotatoConfirmH = kPotatoConfirmItemH * kPotatoConfirmItems;
constexpr int kPotatoConfirmX = kShowVolX;
constexpr int kPotatoConfirmY = kShowVolY + kTransportBtnH + 2;

constexpr int kLogoX = 8, kLogoY = 8;
// LitePic: an upward-pointing triangle, the mirror image of PICMIN's
// downward one at kMinimizeX right next to it - reads visually as a
// matched pair of window-control chevrons. In the original it toggled the
// Windows systray icon (LitePic_Click -> ShowTaskBarIcon), which has no
// equivalent here; repurposed at the user's explicit request as the
// "maximize" control (see toggleMaximize in main.cpp) - the tiny
// fixed-size skin has nothing to genuinely OS-maximize into, so this
// shows/hides EQ+Playlist together instead. A deliberate behavior
// substitution on an existing control, not a new button.
constexpr int kLiteX = 289, kLiteY = 8;
constexpr int kShockX = 305, kShockY = 27;

// Window chrome: real xmp.frm coordinates for MinimizzaPic/MenuBarPic
// (300,8 / 312,8), now actually wired up - the windows are borderless
// (matching the original's BorderStyle=0), so these are the only way to
// minimize/close, not decoration. MenuBarPic's position is identical
// across xmp.frm/Listone.frm/frmEQ.frm, and all three windows now share
// kWindowW, so kCloseX/Y is reused for all three.
constexpr int kMinimizeX = 300, kMinimizeY = 8; // main window only
constexpr int kCloseX = 312, kCloseY = 8, kCloseSize = 10;
// Clicking anywhere in this top strip that isn't a button starts a window
// drag (there's no OS title bar to drag by anymore) - approximates the
// original's Form_MouseMove drag-trigger zone without replicating its
// exact narrow hit-strip geometry.
constexpr int kDragStripH = 20;

// New (not in the original): Winamp-style PL/EQ toggle buttons on the main
// window, since neither Listone.frm nor frmEQ.frm exposed a way to reopen
// themselves once hidden - the original relied on a timer auto-reshowing
// the playlist, which doesn't fit this multi-window-with-real-close model.
constexpr int kPlToggleX = 240, kPlToggleY = 130, kPlToggleW = 36, kPlToggleH = 14;
constexpr int kEqToggleX = 280, kEqToggleY = 130, kEqToggleW = 36, kEqToggleH = 14;

// Linux-only View/Effects dropdown triggers (Linux has no native menu bar
// to append "View"/"Effects" items onto the way app/main_menu.h does on
// macOS - see main.cpp's #ifndef __APPLE__ handling - so these are a
// same-row, same-size twin of kPlToggle/kEqToggle above, sitting in the
// gap between the peak meter (kPeakX+kPeakW=121 below) and kPlToggleX.
constexpr int kViewToggleX = 160, kViewToggleY = 130, kViewToggleW = 36, kViewToggleH = 14;
constexpr int kEffectsToggleX = 200, kEffectsToggleY = 130, kEffectsToggleW = 36, kEffectsToggleH = 14;

// Their dropdowns: same chrome/geometry convention as kEjectMenu* above,
// opening upward from the toggle row (like the Eject menu opens upward
// from the transport row) into the space above it.
constexpr int kViewMenuItemH = 12;
constexpr int kViewMenuItems = 7; // 100%, 200%, 300%, 400%, Zoom In, Zoom Out, Reset
constexpr int kViewMenuW = 64;
constexpr int kViewMenuH = kViewMenuItemH * kViewMenuItems;
constexpr int kViewMenuX = kViewToggleX;
constexpr int kViewMenuY = kViewToggleY - kViewMenuH;

constexpr int kEffectsMenuItemH = 12;
constexpr int kEffectsMenuItems = 5; // xSound, Reverb, Saturation, Compression, Chorus
constexpr int kEffectsMenuW = 76;
constexpr int kEffectsMenuH = kEffectsMenuItemH * kEffectsMenuItems;
constexpr int kEffectsMenuX = kEffectsToggleX;
constexpr int kEffectsMenuY = kEffectsToggleY - kEffectsMenuH;

// The former "FX" toggle now opens a two-level "Options" menu (matching
// the real macOS menu bar's Options>{Effects,Playback} structure - see
// app/main_menu.h) instead of jumping straight to the Effects list: a
// top-level 2-row picker ("EFFECTS"/"PLAYBACK") drills into either the
// existing 5-row Effects list above (unchanged position/size) or this
// new 2-row Playback list. kEffectsToggleX/Y/W/H stays the on-screen
// button's rect (just relabeled "FX"->"OP" at its drawToggle call site).
constexpr int kOptionsMenuItemH = 12;
constexpr int kOptionsMenuTopItems = 3; // Effects, Playback, Potato
constexpr int kOptionsMenuTopW = 64;
constexpr int kOptionsMenuTopH = kOptionsMenuItemH * kOptionsMenuTopItems;
constexpr int kOptionsMenuTopX = kEffectsToggleX;
constexpr int kOptionsMenuTopY = kEffectsToggleY - kOptionsMenuTopH;

constexpr int kPlaybackMenuItems = 4; // Repeat, Random, Smooth Transition, Show Play Counter
constexpr int kPlaybackMenuW = kEffectsMenuW;
constexpr int kPlaybackMenuH = kOptionsMenuItemH * kPlaybackMenuItems;
constexpr int kPlaybackMenuX = kEffectsToggleX;
constexpr int kPlaybackMenuY = kEffectsToggleY - kPlaybackMenuH;

// New (not in the original): low-resource toggles for underpowered/older
// machines - see main.cpp's kFrameBudgetMs and drawFrame's visPanel
// dispatch. Same leaf-menu geometry convention as kPlaybackMenu* above.
constexpr int kPotatoMenuItems = 3; // 30 FPS, Cheap Visualizer, Force Software Rendering
constexpr int kPotatoMenuW = kEffectsMenuW;
constexpr int kPotatoMenuH = kOptionsMenuItemH * kPotatoMenuItems;
constexpr int kPotatoMenuX = kEffectsToggleX;
constexpr int kPotatoMenuY = kEffectsToggleY - kPotatoMenuH;

// xmDisplay(0)/(1): title marquee and status line.
constexpr int kMarqueeX = 16, kMarqueeY = 26, kDisplayFieldW = 278;
constexpr int kStatusX = 16, kStatusY = 35;

// lnPosizione (seek track line) + PicPosizione (thumb).
constexpr int kSeekY = 52, kSeekX0 = 11, kSeekX1 = 301;

// LCD readout cluster (Durata, lblMode, xmDVol, BitRate, lblFreq) - all
// xmDisplay instances, rendered with the same bitmap font as the marquee.
constexpr int kDurationX = 207, kDurationY = 69, kDurationW = 37;
// picModo: the audio-mode icon (Stereo/Mono/XSound), immediately left of
// lblMode - Left=3675/Top=1030/165x165 twips in xmp.frm, i.e. flush against
// kModeLabelX with no gap (245 + 11 == 256). The original's icon bitmaps
// (MODO0/STEREOON/MONOON/XSOUNDON) are compiled VB6 .RES resources, not
// loose files like the rest of assets/skin, so this renders a single
// bitmap-font letter (S/M/X) in the same cell instead of a hand-drawn icon.
constexpr int kModeIconX = 245, kModeIconY = 69, kModeIconSize = 11;
constexpr int kModeLabelX = 256, kModeLabelY = 69, kModeLabelW = 33;
constexpr int kVolTextX = 203, kVolTextY = 92, kVolTextW = 33;
constexpr int kBitRateX = 240, kBitRateY = 92, kBitRateW = 27;
constexpr int kFreqX = 266, kFreqY = 92, kFreqW = 27;

// xmsVol (xmSlide.ctl instance): up arrow, thumb track, down arrow.
constexpr int kVolSliderX = 294, kVolSliderY = 61, kVolSliderW = 16, kVolSliderH = 49;
constexpr int kVolArrowSize = 14;

// Bordered panel behind the Time/Mode/BitRate/Freq readout cluster - a real
// screenshot of the original app shows these grouped in one bevelled box,
// not floating loose text as an earlier pass here had them.
constexpr int kReadoutPanelX = 199, kReadoutPanelY = 65, kReadoutPanelW = 98, kReadoutPanelH = 42;

// analyzer (spectrum bars) and the bottom L/R peak meter row (abuff).
constexpr int kAnalyzerX = 16, kAnalyzerY = 72, kAnalyzerW = 89, kAnalyzerH = 23;
// kPeakX == kAnalyzerX: shifted right from its original 8px (user request,
// after adding the L/R labels - see their kPeakLabelX comment in main.cpp)
// so the bars line up with the analyzer box and transport row above, which
// already share this same left edge (kTransportX0 is also 16). The
// analyzer's own dashed divider (kAnalyzerDividerX=112, below) only spans
// the analyzer box's height, not this row, so the wider row this shift
// produces (right edge 121 instead of 113) doesn't cross anything.
constexpr int kPeakX = kAnalyzerX, kPeakY = 124, kPeakW = 105, kPeakH = 23;
// Dashed vertical divider between the analyzer and the readout/transport
// zone, visible in the original screenshot.
constexpr int kAnalyzerDividerX = 112;

// Playlist window (Listone.frm). The original docks this directly under
// the main window at the same width (Me.Width = xmp.Width); height here is
// a fixed choice (the original stretched to fill whatever space was left
// on screen, which doesn't translate to a fixed-layout port).
constexpr int kPlaylistWindowW = kWindowW;
constexpr int kPlaylistWindowH = 140;
// List starts below kDragStripH (a real header bar, not bare content
// starting at the window edge - needed so the drag zone doesn't eat clicks
// on the first couple of list rows). Shortened from 134 (13 rows) to 80
// (8 rows) - screen-fit trumps row count, and it still scrolls.
constexpr int kPlaylistListX = 8, kPlaylistListY = 24, kPlaylistListW = 314, kPlaylistListH = 80;
constexpr int kPlaylistRowH = 10;
// Per-row play counter (Playback > Show Play Counter): a right-aligned,
// dimmed count reserving room for up to 4 digits ("9999") plus a small gap
// from the track label - only eats into the row's text width when the
// toggle is actually on, same spirit as the scrollbar's conditional
// maxChars/row-width above.
constexpr int kPlaylistCounterChars = 4;
constexpr int kPlaylistCounterGap = 3;
// Real button size (PICCLEAR/PICDELETE/PICSAVE/FRECCIAUP/FRECCIADWN/
// PICSEEK are all 14x14) - an earlier pass here sized the hitboxes to 23px
// (copied from the transport row's 23x13 icons) without checking these are
// a different, smaller asset. Visual left-to-right order (by the .frm's
// design-time Left values, not CommandImg index order) is Clear, Delete,
// Save ("Manage List"), Up, Down, Seek ("jump to now playing") - matches a
// real screenshot's "C D S up down seek" row. Save/Seek were missing
// entirely from an earlier pass; only Clear/Delete/Up/Down existed.
constexpr int kPlaylistBtnY = 108, kPlaylistBtnW = 14, kPlaylistBtnH = 14, kPlaylistBtnPitch = 16;
constexpr int kPlaylistClearX = 8;
constexpr int kPlaylistDeleteX = kPlaylistClearX + kPlaylistBtnPitch;
constexpr int kPlaylistSaveX = kPlaylistDeleteX + kPlaylistBtnPitch;
constexpr int kPlaylistUpX = kPlaylistSaveX + kPlaylistBtnPitch;
constexpr int kPlaylistDownX = kPlaylistUpX + kPlaylistBtnPitch;
constexpr int kPlaylistSeekX = kPlaylistDownX + kPlaylistBtnPitch;

// Save button's popup menu (Quick Save / Save As...) - new, not in the
// original (frmMenu.mnuPlayList was a real Win32 popup menu; there's no
// menu system here). No free chrome exists to place this without
// overlapping something (the window is only 140px tall, and the list and
// button row already sit only 4px apart) - anchored flush against the
// list area's own bottom edge so it never encroaches into the button row,
// at the cost of covering the bottom two list rows while open.
constexpr int kPlSaveMenuW = 64, kPlSaveMenuItemH = 12;
constexpr int kPlSaveMenuH = kPlSaveMenuItemH * 2; // Quick Save, Save As...
constexpr int kPlSaveMenuX = kPlaylistSaveX;
constexpr int kPlSaveMenuY = kPlaylistListY + kPlaylistListH - kPlSaveMenuH;

// Clear button's "Are you sure?" confirmation popup (TODO: "clear
// playlist pops a window up that asks 'Are you sure you?'") - same popup
// style/row height as the Save menu above (kPlSaveMenuItemH), anchored
// under Clear instead of Save, tall enough for a label row ("CLEAR
// PLAYLIST?", 15 chars) plus YES/NO.
constexpr int kPlClearConfirmItems = 3; // label + YES + NO
constexpr int kPlClearConfirmW = 84;    // fits "CLEAR PLAYLIST?" (15 chars * 5px + margin)
constexpr int kPlClearConfirmH = kPlSaveMenuItemH * kPlClearConfirmItems;
constexpr int kPlClearConfirmX = kPlaylistClearX;
constexpr int kPlClearConfirmY = kPlaylistListY + kPlaylistListH - kPlClearConfirmH;

// xmDInfo(0)/(1): two scrolling status-line panels at the bottom right,
// visible in a real screenshot - not implemented at all in an earlier pass.
constexpr int kPlInfoX = kPlaylistSeekX + kPlaylistBtnPitch + 8;
constexpr int kPlInfoW = kPlaylistWindowW - kPlInfoX - 8;
constexpr int kPlInfoY0 = kPlaylistBtnY - 2, kPlInfoY1 = kPlaylistBtnY + 12, kPlInfoH = 12;

// Playlist scrollbar (xmListBox's embedded ScrollBar, an xmSlide instance).
// Real behavior per xmListBox.ctl: only shown when the list holds more rows
// than fit (`UBound(g_List) > g_VisibleRows`), anchored flush against the
// list's own right edge and eating into the row-text width rather than
// widening the window (`g_RightTab` shifts left by ScrollBarWidth only
// while it's actually visible - mirrored here as a conditional maxChars/
// row-width in drawPlaylistFrame, not a permanent layout change). Sized to
// the same 14x14 as the other real button assets (not a smaller custom
// track) since its up/down arrows are literally FRECCIAUP/FRECCIADWN - the
// same icons already reused for the volume slider and the Up/Down
// move-track buttons above (skin.volUp/volDown) - and this app never
// scales blits, so drawing them at any other size would clip/distort them.
constexpr int kPlaylistScrollW = kPlaylistBtnW;
constexpr int kPlaylistScrollArrowH = kPlaylistBtnH;
constexpr int kPlaylistScrollX = kPlaylistListX + kPlaylistListW - kPlaylistScrollW;

// EQ window (frmEQ.frm): 10-band graphic EQ. Width matches kWindowW so it
// stacks flush with the main and playlist windows (Winamp-style: Main / EQ
// / Playlist, edge to edge) rather than the original's exact behavior
// (Me.Top/Left/Height/Width = xmp's, drawn directly on top of the main
// window - independent SDL windows don't have the VB form z-order/owner
// behavior that made that overlap sensible). Presets are a horizontal row
// under the sliders rather than the original's side panel, since a 330px
// width doesn't fit both side by side the way the original's wider,
// un-stacked frmEQ did.
constexpr int kEqWindowW = kWindowW, kEqWindowH = 130;
constexpr int kEqLegendX = 4;
constexpr int kEqSliderX0 = 36, kEqSliderPitch = 28, kEqSliderW = 16;
// Track height trimmed from 90 to 65 (less thumb travel resolution, but a
// real screen-fit issue trumps that) to shrink the window from 170 to 130.
constexpr int kEqSliderTrackY = 22, kEqSliderTrackH = 65;
constexpr int kEqArrowSize = 14;
constexpr int kEqFreqLabelY = 91;
// Preset buttons narrowed from 60 to 50 (still comfortably fits the
// widest label, "TREBLE") to share the row with the per-song EQ toggle
// below, rather than growing the window for a whole extra row.
constexpr int kEqPresetX0 = 6, kEqPresetY = 103, kEqPresetBtnW = 50, kEqPresetH = 18, kEqPresetGap = 3;
// Per-song EQ toggle: same row as the presets, in the width they freed up
// on the right. Same fixed-label/color-only-active convention as
// drawToggle's PL/EQ buttons on Main (label doesn't change with state),
// since "SONG" is too short to also spell out on/off - color carries it.
constexpr int kEqPerSongX = kEqPresetX0 + 5 * (kEqPresetBtnW + kEqPresetGap), kEqPerSongY = kEqPresetY;
constexpr int kEqPerSongW = kEqWindowW - kEqPerSongX - 6, kEqPerSongH = kEqPresetH;

// Info window (new - not in the original as a fourth always-visible window;
// frmInfo.frm was a modal popup, toggled here instead like EQ/Playlist).
// The original's frmInfo also shows raw MPEG-frame-header detail (VBR,
// Emphasis, CRC, Padding) that still isn't available here - dr_mp3
// decodes audio but doesn't expose the frame header - but TODO's "reports
// ID3 values if present" (Title/Artist/Album/Genre/Track) is now covered
// via audio::ReadTrackTags, alongside what's genuinely read from the open
// decoder (File/Format/Mode/Frequency/average Bit Rate/Duration/Decoder).
// Height grown from 100 (7 fixed lines) to fit up to 5 more ID3 lines,
// shown only when actually present - a track with no tags at all still
// just shows the original 7.
constexpr int kInfoWindowW = kWindowW;
constexpr int kInfoWindowH = 150;
constexpr int kInfoTextX = 8, kInfoTextY0 = 26, kInfoLineH = 10;

// About window (TODO: "create about page linked to the japanese character
// click ... with this logo ... and version"). Not present in the original
// at all (no frmAbout in the VB6 source) - opened by clicking the same
// LOGOJAP/"shock" icon (kShockX/Y above) that in the original just
// retriggered a random title-scroll effect; toggled like EQ/Playlist/Info
// rather than modal. Layout mirrors the About window mockup published
// earlier this session: icon+title row, version line, a dashed divider,
// the Zolnetwork brand mark, then a credit line - see drawAboutFrame in
// main.cpp.
constexpr int kAboutWindowW = kWindowW;
// aboutZLogo went through two resize rounds after the user twice reported
// it unreadable: 100x100, then a naive 200x200 (still resizing the whole
// mostly-black 1024x1024 source canvas - the actual artwork only occupies
// roughly its middle 26-31%, so even at 200x200 the real content was
// still tiny). Now cropped to just the artwork's real bounding box first,
// then resized to 208x240 - a far milder downscale that actually keeps
// the tagline text legible. Non-square, so window height and everything
// below the divider grew to match.
constexpr int kAboutWindowH = 350;
constexpr int kAboutIconX = 10, kAboutIconY = 28; // 32x32, aboutIcon's native size
constexpr int kAboutTitleX = 50, kAboutTitleY = 41;
constexpr int kAboutVersionX = 10, kAboutVersionY = 66;
constexpr int kAboutDividerY = 78;
constexpr int kAboutZLogoY = 85; // aboutZLogo is 208x240, centered horizontally
constexpr int kAboutCreditY = 333;

} // namespace xmad::app::layout
