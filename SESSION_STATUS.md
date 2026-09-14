# X.MaD Player Revival — session status

Resume point for a very long session. Point me at this file to pick back up.

## What this is

A cross-platform (macOS/Linux) clone of the user's original VB6 "X.MaD Player"
(`xmplayer/` — sibling directory, the original VB6 source, used as a
functional spec, not ported literally). Stack: C++20 + SDL2, self-contained,
no heavy runtime. UI must be pixel-faithful to the real app (verified against
two real reference screenshots the user provided).

Project root: `xmad-revival/` (sibling to `xmplayer/`).

## Hard requirements the user has stated (don't relitigate these)

- UI must look like the original, not a redesign. Verified against reference
  screenshots multiple times; several of my early guesses were wrong and
  corrected (see "Corrections" below).
- "Fast and self-contained as much as possible."
- All 6 of the original's SpecMode visualizer sub-modes must exist
  (PeakFalls/NoPeakFalls/PeakNoFalls/FadeFft/Oscilloscope/StereoOscilloscope),
  not a simplified 3-mode version.
- Stereo oscilloscope: both channels plain green, separated only by a 1px
  offset — not color-coded/divided (I got this wrong once, user corrected it).
- Main/EQ/Playlist are three separate always-visible SDL windows (not
  one-on-top-of-another), stacked vertically: Main → EQ → Playlist. A
  side-by-side EQ/Playlist layout was explicitly rejected as "ugly."
- Windows must actually fit on the user's screen (1512×982). Currently native
  1x scale, no window-size fudging.
- Windows should "dock magnetically" when dragged close together, and when
  the Main window is dragged, any windows currently docked to it move with
  it as a rigid group — but dragging EQ or Playlist individually only moves
  that one window (it undocks).
- User is the original author of the VB6 app. Treat the VB6 source as ground
  truth for behavior; when unsure, grep the original before guessing.

## Verification discipline established this session (keep following this)

Never claim something works without one of:
1. A headless `--dump-frame` / `--dump-playlist-frame` / `--dump-eq-frame`
   pixel-level check via real `SDL_RenderReadPixels`.
2. A debug CLI hook that drives the **real** production lambda/handler (not
   a reimplementation) — e.g. `--click`, `--playlist-click`, `--drop`,
   `--auto-advance-test`, `--clear-reload-test` (see full list below).
3. An isolated unit test for pure logic (window_snap, file_dialog, fft, eq).

The user has caught unverified claims before; don't skip this.

## Current architecture

```
xmad-revival/
  include/app/   layout.h (all pixel constants), playlist.h, skin.h,
                 window_snap.h (magnetic docking, pure/testable),
                 file_dialog.h (native file picker, pure parsing split out),
                 session.h (save-settings-on-exit, pure parsing split out)
  include/audio/ decoder.h, mp3_decoder.h, flac_decoder.h, ring_buffer.h,
                 engine.h, equalizer.h, stereo_widen.h (clean-room XSound
                 approximation, pure/testable)
  include/dsp/   fft.h (clean-room FFT, NOT ported from the VB6/C++ FFT —
                 both original candidates were third-party or buggy dead code)
  include/gfx/   image.h, bitmap_font.h (ports xmDisplay.ctl's glyph atlas)
  src/main.cpp   ~2070 lines: all 4 SDL windows (Main/EQ/Playlist/Info), all
                 rendering (drawFrame / drawPlaylistFrame / drawEqFrame /
                 drawInfoFrame), all event handling (processEvent), all
                 debug CLI hooks
  src/app/, src/audio/, src/dsp/, src/gfx/   implementations
  tests/         7 test binaries, all passing (see below)
  assets/skin/   real bitmap assets extracted from the original
  tests/fixtures/ tone.{mp3,flac,wav} (1s/880Hz), tone_long.{mp3,wav}
                 (5s/440Hz), track_a/b/c (copies, for playlist testing)
```

Window sizes (native 1x pixels, no scaling): Main 330×155, EQ 330×130,
Playlist 330×140. Stacked vertically at startup, top-anchored on the primary
display's usable bounds.

## What's implemented and working

- BMP loader (24bpp + 8bpp RLE8), bitmap font renderer, skin asset loading.
- Audio: MP3 (dr_mp3) + FLAC (dr_flac) decode, ring-buffer decode thread,
  real-time SDL audio callback, 10-band EQ (RBJ peaking biquads, verified
  ±12dB exactly via `eq_test`), clean-room FFT spectrum analyzer (verified
  against a brute-force reference DFT via `fft_smoke_test`).
- All 6 visualizer SpecModes, correct distinct physics per mode.
- Playlist: add/clear/remove/move-up/down/M3U load-save, in-memory model.
  6 playlist buttons (Clear/Delete/Save/Up/Down/Seek) — all real, all wired.
- Transport: Back/Play/Stop/Next/Pause/Eject, wraparound Back/Next.
- **Eject button now opens a real native "Add to Playlist" file dialog**
  (osascript/NSOpenPanel on macOS, zenity→kdialog on Linux) — no more
  drag-and-drop-only. Parsing logic split into testable `app::ParseDialogOutput`
  (`file_dialog_test`, 8 checks) vs. the live OS-shelling `OpenNativeFileDialog`.
- **Auto-advance on track end**: when a track finishes naturally (not via
  Stop), the next playlist entry auto-plays, wrapping at the end. Verified
  live against real decode/playback timing (`--auto-advance-test N`) — a
  3-track playlist correctly cycles 0→1→2→0→1... at real track-duration
  boundaries. Distinguished from a manual Stop via a `userStoppedTrack` flag
  (both look like `state()==Stopped` otherwise).
- **Magnetic window docking + group-drag**: dragging Main pulls along any
  windows currently docked to it (rigid group move); dragging EQ/Playlist
  individually only moves that window and undocks it. Geometry in
  `app::FindDockedGroup`/`IsDocked` (window_snap.h/cpp), fully unit-tested
  (6 window_snap_test cases + 4 more this session).
- **HiDPI-correct rendering**: `SDL_WINDOW_ALLOW_HIGHDPI` + a helper that
  queries `SDL_GetRendererOutputSize` and scales draws to match — fixes
  blurry rendering on Retina displays. All 3 windows + the `--dump-*-frame`
  debug paths updated.
- **All 3 windows raise together** on any click (fixes EQ not coming to
  front with Main), plus `SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH` set before
  `SDL_Init` (fixes needing 2 clicks to interact with a background window
  on macOS — the classic "first click just focuses" OS quirk).
- **Fixed stale-track bug**: Clear / Delete-current-track / drop-a-replacement-
  m3u now call the new `Engine::Close()` (fully releases the decoder) instead
  of `Engine::Stop()` (which deliberately *keeps* the decoder loaded, so Play
  can restart the same track — correct for the Stop button, wrong for Clear).
  Previously, clearing the playlist and loading a new song then pressing
  Play would resurrect the old, cleared song. Verified two ways: engine-level
  (`engine_smoke_test`'s Close()-then-reopen check) and full end-to-end
  through the real Clear→drop→Play handlers (`--clear-reload-test <path>`).
- **Volume slider + Mute button wired up** (first TODO item). `Engine` gained
  `SetVolume`/`Volume`/`SetMuted`/`IsMuted`; the audio callback scales
  post-EQ samples by the effective volume (0 if muted) before both the
  device and the vis-snapshot capture, so muting also flatlines the
  visualizer. Slider supports click-to-jump, drag-follow, and up/down arrow
  stepping (main window, `kVolSliderX/Y/W/H`); Mute gets a persistent green
  outline while active (mirrors `IlluminaPulsante`'s `LockButton`). Default
  volume 0.25 matches Form_Load's fresh-install INI default (25/100).
  Divergence from the original: manually touching the slider always clears
  Mute (the original's `vsGhost_Change` leaves `Mute` latched even after a
  manual drag, which reads as a quirk, not intended behavior). Verified via
  `--set-volume N` and `--click mute` (both drive the real handler lambdas)
  plus a `--dump-frame` pixel check confirming the thumb moves to the
  correct row for several volumes and that muting parks it at the bottom
  and lights the persistent Mute border.
- **Info button wired up** (second TODO item): opens a new 4th SDL window
  (`infoWindow`/`infoRenderer`, positioned to the right of the Main/EQ/
  Playlist column since it's an on-demand popup, not part of the required
  always-visible stack/docking group), toggled open/closed by the Info
  button via the same `toggleSecondaryWindow` used for the PL/EQ toggle
  buttons. Shows File/Format/Mode/Frequency/average Bit Rate/Duration/
  Decoder for the currently-open track, or "No track loaded." otherwise -
  all genuinely read from the open `Decoder` (average bit rate is computed
  from file size / duration, not read from a header field). Flagged
  divergence: the original's `frmInfo` also showed MPEG-frame-header and
  ID3 detail (VBR flag, Emphasis, CRC, Track, Padding, Artist, Album,
  Genre, Comment, Year, CopyRight, Frames, ...) that isn't available here -
  dr_mp3/dr_flac decode audio but don't parse ID3 tags or raw MPEG frame
  headers (same underlying gap as the already-flagged "ID3 tag editing not
  implemented"). Verified via `--dump-info-frame <path>` (pixel-confirmed:
  a track-less run shows exactly one text row, a loaded track shows seven,
  and an MP3 vs. FLAC dump of the same content actually differ at the
  pixel level - not a static template) and `--click info` (toggles the
  window, drives the real `handleUtilityPress` lambda).
- **Session persistence wired up** (third TODO item: "save settings on
  exit"). New `app::Settings`/`SerializeSettings`/`ParseSettings`/
  `SaveSettingsFile`/`LoadSettingsFile` in `include/app/session.h` +
  `src/app/session.cpp` (pure serialize/parse split from file I/O, same
  shape as `file_dialog.h`'s `ParseDialogOutput`). Persists specMode
  (visualizer), volume, whether it was playing or stopped, and the current
  playlist index to `~/.xmad-revival/settings.cfg`, plus the playlist
  itself to `~/.xmad-revival/session.m3u` via the existing `SaveM3U`/
  `LoadM3U`. Saved unconditionally on every normal exit; loaded on startup
  only when nothing else already said what to open (no explicit
  `--playlist`/`--play`/positional track) and never during a
  `--dump-*-frame` headless run (those need a deterministic empty-playlist
  starting point, not whatever a real session left on disk). Deliberate
  divergence from the original: skips `GestisciPosFrm`'s exact mid-track
  resume position, since that's gated behind a "SSTREAMPOS" preference
  that defaults **off** in the original itself - the faithful default is
  "a track that was playing restarts from the beginning on relaunch," not
  seek-to-exact-sample. Mute is also intentionally not persisted (the
  original explicitly comments out `SetINI(..., "MUTE", ...)` too). Window
  position, Repeat/Random, and ShowTask aren't persisted either - not
  implemented / not asked for. Verified with real file I/O end-to-end
  under an isolated `HOME` (never the real one): a session with
  `--set-volume`/`--specmode-clicks` that exits via `--auto-advance-test`'s
  natural deadline writes real `settings.cfg`/`session.m3u` files that a
  second, argument-less launch then reads back and applies (confirmed via
  a "Resumed session: ..." startup log and the resulting `engine.state()`)
  - both the playing-resumes-and-autoplays and stopped-stays-stopped cases
  checked, plus confirming an explicit track argument or any
  `--dump-*-frame` flag correctly skips the resume path entirely. Pure
  parse/serialize logic also covered by `tests/session_test.cpp` (8th test
  binary, `make test`).
- **Playlist Delete/Save/Seek buttons**: turned out already implemented and
  wired from earlier work (`handlePlaylistButtonPress` cases 1/2/5 in
  main.cpp) - the TODO line asking for them was stale. Re-verified live via
  `--playlist-click delete|save|seek` against the real handler: Delete
  correctly closes the engine when the deleted row is the one playing,
  Save writes a real M3U, Seek ("find current song") selects/scrolls to
  `playlist.currentIndex()`.
- **Audio-mode icon (Stereo/Mono/XSound)**: new `kModeIconX/Y/Size` in
  layout.h, immediately left of the MODE label (flush - 245+11==256, no
  gap, matching xmp.frm's picModo coordinates). First shipped as a
  bitmap-font letter (S/M/X) since the original's icons (MODO0/STEREOON/
  MONOON/XSOUNDON) are compiled into `Source/xmP.RES`, not loose files
  like the rest of `assets/skin/` - but the user (correctly) called the
  letters ugly, so these are now the **real extracted bitmaps**: xmP.RES
  turned out to be a standard 32-bit Win32 `.RES` file (`file` reports
  "MSVC .res"), which is a well-documented, generically-parseable format -
  wrote a one-off Python script (not checked into the repo; a pure
  asset-extraction step, not app logic) that walks its resource-entry
  headers, finds `RT_BITMAP` (type 2) entries by name, and synthesizes the
  14-byte `BITMAPFILEHEADER` a loose `.bmp` needs (the resource data is
  just a `BITMAPINFOHEADER` + palette + pixels - LoadResource's "packed
  DIB" format). Copied straight into `assets/skin/` as `mode_none.bmp`/
  `mode_mono.bmp`/`mode_stereo.bmp`/`mode_xsound.bmp` (MODO0 was 1bpp,
  outside the loader's supported 8/24bpp - re-saved as 8bpp with an
  explicit black/white palette, pixel-identical, rather than extending
  the loader for a single one-off asset). These aren't text at all - each
  is an 11x11 pictogram: a plain white frame for "no track" (Azzerato), a
  frame with 1 or 2 orange filled squares lit inside for Mono/Stereo, and
  Stereo's two squares plus a small center square for XSound - orange
  turned out to be a real, deliberate color in the extracted palette, not
  an artifact of a bad guess at it (confirmed against the file's own color
  table, and other extracted-in-passing icons like PICBACK/PICCLEAR
  matched this app's already-verified existing assets exactly in size,
  cross-confirming the parser). Mode is `engine.channels()==1 ? Mono :
  Stereo` when a track is open, MODO0 when not (previously blank - now
  matches the original exactly, which always assigns picModo *some*
  picture), and XSound - mirroring `mnuXSound.Checked` - overrides both
  when toggled, bound to the same "x" key the original's *global* hotkey
  used (here: window-focused only, the same simplification already made
  for Escape-to-quit, since there's no menu system to host a real
  checkbox). Verified via `--toggle-xsound <N>` (drives the same
  `toggleXSound` lambda a real keypress calls) plus `--dump-frame` pixel
  crops of the icon cell for all three states, confirmed against the
  known extracted bitmaps.
- **XSound now has a real audio effect** (follow-up to the icon above, at
  the user's request - initially shipped as icon-only). New
  `audio/stereo_widen.h`/`.cpp`: a clean-room mid-side stereo widener
  (`ApplyStereoWiden` - boosts the L-R "side" energy relative to the L+R
  "mid" energy, clamped to [-1,1]), NOT a port of the original's actual
  XSound DSP (that lives in the proprietary xmMP3.dll and isn't
  available) - substitutes for `mXSrnd`'s `XSOUND_SURROUND` flag
  specifically. The original's XSound checkbox/hotkey only ever toggled
  that one flag; a separate `XSOUND_NORMALIZE`/Level feature (loudness
  normalization) also lives behind the same enum but is driven
  independently elsewhere in the original and isn't part of this toggle
  there either, so it's out of scope here too - not a gap, a match.
  `Engine::SetXSound`/`XSound()` (atomic bool) gate a call to
  `ApplyStereoWiden` in the decode thread right after `eq_.Process`, same
  slot EQ occupies; a no-op on mono content. `engine.XSound()` is the
  single source of truth for both the DSP and the mode icon - no separate
  UI-side flag to drift out of sync. Verified three ways: pure math in
  `tests/stereo_widen_test.cpp` (10th test binary - identity at width=1,
  zero-effect on perfectly in-phase/mono content, correct widened values,
  and clamping at the output range); and, more importantly, on **real
  decoded, real-time-played audio** through the actual production
  `Engine` (a scratch harness, not part of the permanent suite): seeking
  into a real stereo FLAC past its intro and comparing
  `GetVisSnapshot`'s average `|L-R|` before vs. ~1.5s after
  `SetXSound(true)` (long enough to drain the ~1s ring buffer of
  already-decoded unwidened audio) showed a clear, consistent increase
  (0.0055 -> 0.0067, ~1.2x) - confirming the effect reaches audio that's
  actually played, not just synthetic buffers in a unit test. (A dual-mono
  fixture like `tests/fixtures/track_a.mp3`, where L already exactly
  equals R, correctly shows zero change - side energy is genuinely zero
  there, not a bug.)
- **Playlist rows now show duration** ("MM:SS - filename", exact format
  match for `modFunzioniAccessorie.bas`'s `DurataStream(...) & " - " &
  name`). `Playlist` only stores paths, so duration needs a decoder open
  per file - free for FLAC (duration's in the STREAMINFO header) but a
  real full-file scan for MP3 (`Mp3Decoder::totalFrames()` has no
  frame-count header to read from - see its doc comment), so results are
  cached per path in a new `plDurationCache` and only ever probed for rows
  actually scrolled into view, never the whole list up front. The
  currently-playing row instead reads `engine.durationSeconds()` directly
  (already known for free, no second decoder). Unreadable files show
  "--:--" rather than a bogus duration. Verified via `--dump-playlist-frame`
  pixel crops: three real fixtures (1s/5s/1s) rendered their exact correct
  durations, and a playlist pointed at a nonexistent file rendered
  "--:-- - MISSING.MP3".
- **Playlist rows now show the tagged title if present, else the filename**
  (TODO: "PL has the name of the song..."). New `include/audio/tags.h` +
  `src/audio/tags.cpp`: reads just the display title, nothing else -
  ID3v2 (TIT2, v2.3/v2.4; v2.2's different 3-char-frame-ID shape is out of
  scope, falls through to v1/filename) with ID3v1 fallback for MP3, and a
  VORBIS_COMMENT `TITLE=` lookup via dr_flac's own metadata callback for
  FLAC (no hand-rolled FLAC container parsing needed). Pure parsing
  (`ParseId3v2Title`/`ParseId3v1Title`, byte-buffer in, no file I/O) is
  split from the I/O shell (`ReadTrackTitle`) exactly like
  `file_dialog.h`'s `ParseDialogOutput`/`OpenNativeFileDialog` split -
  covered by `tests/tags_test.cpp` (10th test binary): Latin1/UTF-8/
  UTF-16 TIT2 encodings, ID3v1, and adversarial bounds cases (a frame
  claiming a size larger than the buffer must yield "" instead of
  overreading). Cached per path in `plTitleCache`, same shape and same
  reasoning as the duration cache just above (real per-file cost, only
  probed for rows actually visible). Verified against real files, not
  just synthetic ones: a real Vorbis-tagged FLAC from the user's library
  correctly read "Luv 4 Luv", and a copy of a test fixture with a hand-
  built ID3v2 TIT2 tag spliced onto the front correctly read "Test Title
  Track" *and* still decoded/played normally afterward (dr_mp3 skips the
  tag transparently).
- **Fixed: XSound state wasn't saved/resumed** (user-reported regression
  in the session-persistence feature above - it shipped covering
  specMode/volume/playing/index but XSound was overlooked). Added
  `Settings::xSound` + an `XSOUND=` line in the serialize/parse/round-trip
  (updated `tests/session_test.cpp` accordingly), applied via
  `engine.SetXSound(...)` on resume and read back via `engine.XSound()`
  on save - same shape as every other persisted field. Verified end-to-end
  under an isolated `HOME`: a session with `--toggle-xsound 1` that exits
  via `--auto-advance-test` writes `XSOUND=1` to the real settings file,
  and a second argument-less launch resumes with `xSound=1` in the
  "Resumed session: ..." log.
- **Minimize now cascades symmetrically across all 4 windows, and a new
  "Maximize" shows/hides EQ+Playlist together** (both TODO items, done
  together since they're two faces of one feature). Minimize/restore:
  `processEvent` handles `SDL_WINDOWEVENT_MINIMIZED`/`RESTORED` on *any*
  of Main/EQ/Playlist/Info and minimizes/restores whichever of the
  *other* three the user hasn't separately hidden - mirrors `MinimizzaXMP`
  (which hides `frmListone` alongside minimizing `xmp`), extended to the
  EQ/Info windows this port added. Symmetric-across-all-four is the
  important part: each is a genuinely separate top-level window, so
  macOS gives each its own Dock icon once minimized, and restoring *any
  one* of those icons must bring the rest back too.
  Maximize: **not a new button** - first pass wrongly added one, caught
  and corrected mid-session. It's the *existing* `LitePic` triangle
  (`kLiteX`, right next to `PICMIN`'s downward triangle - they read as a
  visual pair) repurposed via `toggleMaximize`: shows EQ+Playlist together
  if either is hidden, hides both together if both are already shown.
  Flagged divergence: in the original, `LitePic_Click` toggled the
  Windows systray icon (`ShowTaskBarIcon`) - unrelated behavior with no
  macOS/Linux equivalent, deliberately replaced at the user's explicit
  request rather than ported.
  **Three real bugs found on the user's real machine over several rounds,
  all now fixed:** (1) the symmetric-cascade requirement above was
  initially missed - an earlier version only listened for the event on
  Main, so restoring via any *other* window's Dock icon restored just
  that one, leaving Main and the rest still minimized. (2) even after
  fixing (1), it still didn't work: the cascade's "should this window be
  included" check used `SDL_GetWindowFlags(w) & SDL_WINDOW_HIDDEN`, but
  testing directly confirmed `SDL_MinimizeWindow` *also* sets that same
  `HIDDEN` bit on this platform (not just `SDL_WINDOW_MINIMIZED`) - so by
  the time any restore event arrived, every other still-minimized window
  read as "hidden" and got skipped. Fixed by tracking visibility intent
  ourselves (`eqUserVisible`/`plUserVisible`/`infoUserVisible`, set only
  by the user's own show/hide/close actions, independent of transient
  minimized state) and gating the cascade on that instead - the PL/EQ
  toggle buttons' highlighted state was switched to the same tracking for
  consistency (identical latent bug: would've shown "off" while merely
  minimized). (3) **still** didn't work on the real machine after (1) and
  (2): real-machine testing (via a follow-up "still doesn't work" report)
  narrowed it to `SDL_WINDOWEVENT_RESTORED` itself - it was only ever
  observed to actually arrive when *Main* was the window being restored;
  clicking a *secondary* window's own Dock icon apparently doesn't
  reliably deliver it in this multi-window borderless setup. Fixed by
  also reacting to `SDL_WINDOWEVENT_FOCUS_GAINED`, which does fire
  whenever any Dock icon is clicked - gated on new
  `mainMinimized`/`eqMinimized`/`plMinimized`/`infoMinimized` bookkeeping
  (set only by this same code, never read from an SDL flag) so an
  ordinary click on an already-visible window can't misfire it; the
  RESTORED path is kept too, since it's harmless when it does arrive.
  Verified: `--sim-click main 294 12` fired twice through the real event
  path toggled `plHidden`/`eqHidden` 0->1->0 exactly as expected
  (pixel-real coordinates, not a hand-called function); the fixed
  cascade's selection logic was reconfirmed via `--test-minimize-cascade`
  (always paired with `--auto-advance-test` - on its own it falls through
  to the unbounded interactive loop and spins at full CPU forever with no
  way to send it `SDL_QUIT`, learned the hard way mid-session) across all
  four scenarios: Main-minimize -> EQ-RESTORED, and Main-minimize ->
  Info-FOCUS_GAINED, both correctly clearing all four tracked bools.
  Whether the real OS/WM genuinely delivers these specific events for a
  real Dock-icon click on *this exact build* still needs the user's real
  machine to confirm - that's precisely the thing this sandbox (no real
  display attached) has now been wrong about twice.
  **Confirmed working on the user's real machine** after fix (3) - the
  only remaining complaint was cosmetic: the resulting window stacking
  order was distracting (PL, Main, EQ). Restore order is now fixed
  regardless of which window triggered the cascade: Info, then Playlist,
  then EQ (skipping whichever one was actually clicked, since it's
  already frontmost from that click), with Main *always*
  `SDL_RestoreWindow`+`SDL_RaiseWindow`'d last and unconditionally
  (harmless no-op if it's already frontmost) so it consistently ends up
  on top no matter which Dock icon started the cascade. Verified via
  temporary tracing of the actual `SDL_RestoreWindow` call sequence for
  both a EQ-triggered and an Info-triggered restore, confirming the
  intended order in each case (trigger skipped, Main always last).
- **Channel-level LED now uses the original's real green-to-yellow
  gradient** instead of a hand-guessed 2-color (green + a flat amber
  above 85%) split. Root cause of the wrong guess: the L/R level meters
  (`abuff` row, `xmp.frm`'s `SpectrumSin`/`SpectrumDes`) aren't driven by
  any color-threshold code in the original at all -
  `InternalSpectrum.ctl`'s `WriteSpec` just shrinks an opaque "eraser"
  box (`ScancellatorePic`) over a *fixed, never-redrawn* gradient bitmap
  (`PICGPH`, set once via `Modello`), revealing more of it from the
  bottom up as level rises - so the apparent color-at-level comes
  entirely from whatever's baked into that one static image, not from
  logic. Extracted `PICGPH` from `Source/xmP.RES` the same way as the
  mode icons (3x26 px) and read its real pixel data directly: pure green
  for the bottom ~15% (rows 22-25, RGB 0/230/0), pure yellow for the top
  ~15% (rows 0-3, RGB 230/230/0), and a continuous linear ramp on the red
  channel between them - not a hard threshold anywhere, and not
  symmetric (the green plateau and the ramp are different lengths).
  New `include/gfx/level_meter.h` + `src/gfx/level_meter.cpp`:
  `LevelGradientColor(position)`, a pure function reproducing that exact
  26-row table (hardcoded from the real extracted data, not
  re-derived/approximated) rather than loading the bitmap itself - our
  meter renders as 20 discrete LED segments rather than replicating the
  original's reveal-a-bitmap mechanic pixel-for-pixel, so each lit
  segment now just samples this gradient at its own position instead of
  a flat green/amber split. Covered by `tests/level_meter_test.cpp`
  (11th test binary): endpoints, two interior positions checked against
  the exact extracted values, both flat plateaus, and out-of-range
  clamping. Verified visually too: a synthesized full-scale test tone
  (via `ffmpeg`, since fixture tones are quieter) at `--set-volume 100`
  produced a `--dump-frame` pixel-crop showing the full green ->
  chartreuse -> yellow sweep exactly as the extracted data predicts.
  Note: the original's spectrum-bar analyzer (`BitBltSpec`, already
  ported) also draws from this same `gph` bitmap and likely has the
  identical "should be a gradient, currently flat" gap - flagged here
  since it wasn't what this TODO item asked about, not fixed.
- **Main window title now shows the tagged title if present, else the
  filename** - same TODO family as the Playlist row title, just applied
  to the marquee too. `getPlaylistDuration`/`getPlaylistDisplayName` (and
  their caches) were hoisted from down by the Playlist window's own state
  to just above `drawFrame`, since Main's marquee needed to call
  `getPlaylistDisplayName` too and C++ lambdas resolve names lexically -
  a lambda can't reference something declared later in the same function,
  regardless of capturing `[&]`. No behavior change for the Playlist
  window itself, just a relocation so both windows can share one cache
  instead of duplicating the ID3/Vorbis-reading logic. Verified via
  `--dump-frame` pixel crops of the marquee: a copy of a fixture with a
  hand-built ID3v2 tag spliced on showed "MAIN WINDOW TITLE TEST" (the
  tag), and the plain untagged fixture still correctly fell back to
  "TONE.MP3".
- **Oscilloscope amplitude boosted** for visibility, per the user's
  report that it read as too flat/hard to see. A flat `kOscilloscopeBoost
  = 2.0f` gain on the sample value before it's turned into a Y offset,
  clamped to the analyzer's own vertical bounds (`kAnalyzerY` ..
  `kAnalyzerY + kAnalyzerH - 1`) so genuinely loud material - or high
  playback volume - clips flat against the top/bottom rather than
  drawing outside the display. Deliberately a flat gain rather than a
  deeper rework of *where* the snapshot is captured: the vis snapshot is
  taken post-EQ/post-volume (see `Engine::GetVisSnapshot`'s doc comment),
  so the trace's amplitude is tied to the playback volume slider - at
  the default 25% volume a full-scale signal only used a sliver of the
  display even before this. That coupling is arguably a separate,
  deeper divergence from the original (whose `xmMP3_getWave` likely read
  raw decoder-level PCM, upstream of any volume control) - flagged here,
  not fixed, since the user only asked for "a little" boost, not a
  capture-point rework. Verified by measuring the actual rendered green
  trace's peak pixel deviation from the analyzer's center line via
  `--dump-frame`: a full-scale synthesized tone at 100% volume now
  reaches ~91% of the available vertical range without clipping (10 of
  11px), and the same tone at the default 25% volume shows a real,
  measurable (if still modest, given how quiet 25% actually is) increase
  over the unboosted trace.
- **Project now has its own version number**, independent of the
  original's "v1.0.378" (which the UI still shows as-is in its startup
  banner/status-line easter egg - swapping that display over to this new
  version is the *next* TODO item, deliberately not done here). New
  `include/app/version.h`: `xmad::app::kVersion = "0.1.0"` (the user's
  choice - bump by hand at meaningful milestones, nothing automated reads
  or writes it). Wired up concretely via a `--version`/`-v` CLI flag
  (prints `X.MaD Player Revival v0.1.0` and exits 0) rather than adding
  an inert unused constant - verified by actually running it.
- **Main window's idle-state title banner now shows this project's real
  version** ("X-MAD.PLAYER v0.1.0"), not the hardcoded "v1.0.378" it
  used to fall back to when a track is queued but nothing's playing yet
  (`marqueeText`'s default, before either the now-playing title or empty-
  playlist "Nope" override it). **Also (after a follow-up "I still see
  the legacy version" report) the always-visible STATUS line just below
  it** (`texStatus`) **now reads "*** V0.1.0 ALPHA GOJIRA ***"** - the
  line the user was actually seeing in every normal session, since the
  idle banner above it is a rare fallback (only visible when nothing's
  playing yet) while this STATUS line always shows. First pass left this
  one as the original's exact "V1.0.378 STABLE GAMERA" for screenshot
  fidelity (pixel-verified against real reference screenshots earlier in
  this project) and flagged that scoping call explicitly; the user
  confirmed they wanted the version changed to match the banner, then
  separately asked for the original's own release codename ("STABLE
  GAMERA") to become this project's own ("ALPHA GOJIRA") - same kaiju
  theme, own naming, both explicit user requests rather than assumptions.
  Verified via `--dump-frame` pixel crops in the actual normal-use state
  (a track open and playing, not the rare idle state) showing both lines
  together: title reads the track name (unrelated to this change) and
  the STATUS line directly below reads "V0.1.0 ALPHA GOJIRA".
- **The "fake Japanese character" is now the real katakana ダ**.
  Identified what it actually is first: `assets/skin/shock.bmp` (drawn at
  `kShockX`/`kShockY`) is xmp.frm's `LowHigh` control - `ToolTipText`
  says "Shock" (hence this port's field/asset name), but its `Picture`
  is `LOGOJAP`, a compiled `.RES` bitmap. Extracted the real `LOGOJAP`
  the same way as the mode icons to check whether the existing
  `shock.bmp` was a faithful copy or a guess - it was pixel-identical to
  the real resource, confirming the "fake" look wasn't a porting error;
  it's the original author's (the user's) own 16x16 pixel-art attempt at
  a Japanese character, which they've now said reads as illegible/wrong
  and asked to be replaced with the real character ダ specifically,
  regardless of what that original asset contained. Rendered fresh with
  Hiragino Sans (a real system CJK font, used only as an offline asset-
  generation step - nothing in the shipped app links against a font
  engine, preserving "self-contained, no heavy runtime"), downsampled to
  16x16 with antialiasing intentionally kept rather than a hard
  black/white threshold (tried both - thresholding lost the dakuten
  voicing marks that distinguish ダ from タ at this size), saved as an
  8bpp indexed BMP with a green ramp palette matching the app's aesthetic
  and the same uncompressed format the loader already supports. Directly
  overwrote `assets/skin/shock.bmp` in place - no source changes needed
  since it's loaded at runtime. Verified via `--dump-frame` pixel crop at
  the icon's exact real coordinates in a normal running frame: legible as
  ダ (the タ loop plus both dakuten dots visible), not the previous
  abstract squiggle. The *next* TODO item (an About page linked from
  clicking this same icon) is intentionally not addressed here - nothing
  currently reacts to a click there, matching the original's own
  click handler (`LowHigh_Click`, a random title-scroll retrigger) still
  not being ported either.

- **macOS Dock icon = the real extracted skull-and-crossbones `.ico`
  (SKULL resource from `xmP.RES`)**, not the generic Unix-executable
  icon. A bare `build/xmad` binary has no way to carry a custom icon -
  the Dock reads `CFBundleIconFile` from an `.app` bundle's
  `Info.plist`, which SDL can't set at runtime. Added:
  - `assets/icon/AppIcon.icns` - built with `iconutil` from a 10-image
    iconset (16/32/64/128/256/512/1024px), generated from the same real
    32x32/16x16 SKULL.ico frames used earlier this session for the About
    mockup, nearest-neighbor upscaled (not smoothed) so the pixel-art
    look stays crisp and intentional at large Dock/Finder sizes instead
    of blurring into mush.
  - `assets/icon/Info.plist` - a template (`@VERSION@` placeholder for
    `CFBundleVersion`/`CFBundleShortVersionString`, substituted from
    `app::kVersion` at build time via `sed`, so the version string can't
    drift out of sync with `include/app/version.h`) with
    `CFBundleDisplayName`/`CFBundleName` "X.MaD Player Revival",
    `CFBundleExecutable` "xmad", `CFBundleIconFile` "AppIcon".
  - A new `make app` target (macOS-only, guarded by `ifeq
    ($(shell uname -s),Darwin)`) that assembles `build/xmad.app`
    (`Contents/MacOS/xmad` copied from the existing `build/xmad` binary,
    `Contents/Resources/AppIcon.icns`, `Contents/Info.plist`). Does not
    copy `assets/skin` into the bundle - CLI usage (passing a skin dir +
    tracks as positional args) is unchanged and still works identically
    whether you run the bundled binary directly or the bare one.
  - Verified for real, not just asserted: launched `build/xmad.app` via
    `open` (confirmed via `log show` that AppKit actually registered the
    process, `CFBundle`/`CoreAudio` initialized, no plist-parse errors),
    and independently asked macOS itself which icon it resolves for that
    bundle path - `NSWorkspace.iconForFile:`, the same API the real Dock
    and Finder use - and saved that to a PNG. It came back as the real
    skull-and-crossbones, correctly masked into the squircle shape, not
    a fallback/generic icon. `plutil -lint` also confirmed the generated
    `Info.plist` is well-formed.
  - **Fix**: user reported "I see zero changes in the app" - correctly,
    since `make run`/`make all` still built and launched the bare
    `build/xmad` binary, which can never show a custom Dock icon
    regardless of the bundle existing (the bundle was only reachable via
    the new, separate `make app` target). Fixed by making `run`/`all`
    platform-conditional on `$(UNAME_S)`: on macOS they now build/launch
    `build/xmad.app` via `open "$(APP_BUNDLE)" --args ...` (confirmed
    this session that launching through `open` is what makes macOS read
    `CFBundleIconFile` - running the raw binary never does); Linux keeps
    the original bare-binary `run`/`all` unchanged, since there's no
    bundle concept there. So the icon now shows up through the same
    `make run` workflow already in use, with no new command to remember.
  - **Follow-up ("make the app launch without parameters")**: even with
    the bundle wired up, launching it (Dock, Finder double-click, or
    `open build/xmad.app` with no `--args`) would have failed, since
    `main()` defaulted a missing asset-dir argument to the plain relative
    path `"assets/skin"` - only correct if the process's *current working
    directory* happens to be the repo root, which it isn't for any of
    those launch paths (Finder/Dock launches land in an unrelated default
    cwd). Fixed by resolving the default from the running binary's own
    on-disk location instead of the cwd:
    - `ExecutableDir()` (`src/main.cpp`) - `_NSGetExecutablePath` +
      `realpath` on macOS, `readlink("/proc/self/exe")` on Linux.
    - `ResolveDefaultAssetDir()` - tries the bundle's
      `Contents/Resources/skin` first (macOS bundle layout, executable
      sits in `Contents/MacOS/`), then a copy sitting next to the binary
      (e.g. a Linux tarball), then falls back to the original plain
      `"assets/skin"` relative path so the existing terminal-from-repo-
      root dev workflow (`make run`, bare `./build/xmad`) keeps working
      unchanged.
    - `make app` now also copies `assets/skin` into
      `Contents/Resources/skin` so a built `.app` is fully self-contained
      - no external asset directory to keep track of.
    - Empty-playlist launch needed no change - `main()` already treats an
      empty playlist as a no-op branch (falls through to either resuming
      a saved session or just sitting idle), so zero args was always
      going to work once the asset directory itself could be found.
    - Verified for real: ran the *unmodified bundle binary* with zero
      arguments from `/tmp` (outside the repo entirely, isolated `HOME`)
      two ways - once just watching it start clean with no stderr
      output, then again with `--dump-frame` and inspected the actual
      pixels, which came back as a fully-rendered normal frame (fonts,
      buttons, LED meters all present) - not a "failed to load skin"
      error exit. Also re-checked the plain dev path (`./build/xmad`
      from the repo root, no bundle) still resolves via the same
      relative-path fallback as before. Full 11-test suite green
      throughout.

- **About window (TODO: "create about page linked to the japanese
  character click ... with this logo ... and version") - now real,
  wired into the actual app, not just the earlier mockup.** Not present
  in the original at all (no frmAbout in the VB6 source). Toggled like
  EQ/Playlist/Info (`toggleSecondaryWindow`, own `aboutUserVisible`/
  `aboutMinimized` bookkeeping) rather than modal, and fully joins the
  same machinery those three already use: raised together on any window
  click, included in the minimize/restore cascade (`aboutT` in the
  `Target`/`restoreOrder` arrays), draggable by its header, closes via
  its drawn X or `SDL_WINDOWEVENT_CLOSE`.
  - **Opened by clicking the shock/LOGOJAP icon** (`kShockX,kShockY`,
    16x16, already drawn on Main) - in the original that icon's click
    handler (`LowHigh_Click`) just retriggered a random title-scroll
    effect, which was never ported here either (see the shock/ダ entry
    above); this TODO repurposes the same click for About instead of
    adding a new control.
  - **Content**: header "ABOUT" + close icon; the real skull-and-
    crossbones icon (`Skin::aboutIcon`, same SKULL.ico source as the
    macOS Dock icon) next to "X-Mad.Player Revival"; the same formatted
    version string Main's status line uses (`*** V{kVersion} ALPHA
    GOJIRA ***` - built from `app::kVersion` once, not hardcoded a
    second time, so it can't drift); a dashed divider
    (`DrawDashedHLine`); the user-supplied Zolnetwork logo
    (`Skin::aboutZLogo`); a "FHT - Zolnetwork" credit line. Layout
    constants (`kAboutWindow*`/`kAboutIcon*`/`kAboutTitle*`/
    `kAboutVersion*`/`kAboutDivider*`/`kAboutZLogo*`/`kAboutCredit*`) in
    `layout.h` mirror the proportions of the About window mockup
    published earlier this session, adapted to the real 5x6 bitmap font.
  - **New assets** (`assets/skin/about_icon.bmp`, `about_zlogo.bmp`,
    loaded in `Skin::Load`): both 24bpp uncompressed BMPs - the loader
    supports 8bpp-indexed and 24bpp only, and never reads/writes alpha
    (every existing icon in this app is opaque; blend mode is set on
    upload but every loaded pixel's alpha is hardcoded 255), so neither
    can be a transparent PNG dropped in directly. Instead both were
    pre-flattened at generation time onto the app's own window-fill
    color (`0x05,0x06,0x03`, the exact color `Bevel::Draw` fills every
    window's interior with) so they sit seamlessly against the real
    background with no visible box. about_icon.bmp is the real
    SKULL.ico frame at native 32x32 (no scaling - it's already pixel
    art); about_zlogo.bmp is the user's Zolnetwork PNG downscaled to
    100x100 with LANCZOS (a photographic-style source, so smooth
    downscale rather than nearest-neighbor, unlike the pixel-art icon).
  - **User course-corrections during the mockup phase, carried forward
    here**: an AI-generated "Godzilla in the skull's style" icon was
    drawn, iterated through several legibility passes, then explicitly
    rejected ("wack") in favor of the real skull - the About window
    (and the Dock icon) both use that same real extracted SKULL.ico
    throughout, no generated art. The mockup's small header icon also
    went through app_icon.png (the green Z-derived logo.bmp) before
    landing on the skull too, per the user's final call - this real
    window's header icon is the skull, matching that final decision.
  - **Verified for real, three ways**: (1) `--dump-about-frame` after
    `--click about` (a debug hook driving the exact same
    `toggleSecondaryWindow` lambda a real click calls) - inspected the
    actual pixels, all elements present and correctly laid out; (2) the
    *real coordinate hit-test path*, not the debug shortcut -
    `--sim-click main 310 27` (a real synthetic SDL_MOUSEBUTTONDOWN/UP
    pair at the shock icon's actual on-screen position, run through the
    same `processEvent` the interactive loop uses) correctly flipped
    `aboutHidden` from 1 to 0; a click just outside the icon's 16x16 box
    (325,32) left it untouched; two clicks on the icon opened then
    closed it. (3) Re-ran through the macOS `.app` bundle
    (`build/xmad.app/Contents/MacOS/xmad`, zero args, `HOME` pointed at
    an isolated scratch dir, launched from outside the repo) and got
    pixel-identical output to the plain dev binary - confirms the new
    BMP assets load correctly via `make app`'s existing
    `cp -R assets/skin` step with no changes needed there. Full 11-test
    suite green throughout, clean rebuild confirmed.
  - **Follow-up ("the Z logo is unreadable, is too small")**: aboutZLogo
    was 100x100 - too small for its own tagline ("WE WILL HANDLE IT",
    baked into the source art) to stay legible. Regenerated at 200x200
    directly from the original 1024x1024 source (not an upscale of the
    100x100 version - re-downscaled fresh with LANCZOS for full quality),
    which pushed `kAboutWindowH` from 210 to 290 and everything below the
    divider (`kAboutZLogoY`, `kAboutCreditY`) down to match. Verified via
    a fresh `--dump-about-frame` - the tagline text is now clearly
    readable. Note for next time a header-only change happens again:
    `layout.h` isn't tracked as a dependency by the Makefile (no `-MMD`),
    so a constants-only edit needs `touch src/main.cpp` (or `make clean`)
    to actually force a rebuild - `make` alone silently no-ops.
  - **Second follow-up ("still barely readable")**: the 200x200 resize
    was still the wrong fix - it was downscaling the *whole* 1024x1024
    source canvas, but the actual artwork (the two skeletal hands, the Z,
    the tagline) only occupies roughly its middle 26-31%
    (bbox-of-non-black pixels: x 386-651, y 340-653), so even at 200x200
    the real content was still effectively rendering from a ~265px-wide
    subject - the resize target size was never the bottleneck, the huge
    black margins being carried along were. Fixed by cropping to that
    real bounding box first (with a little padding: (361,315)-(676,678),
    315x363) and *then* resizing - to 208x240, a much milder downscale
    that actually preserves the tagline's strokes. Confirmed visually via
    `--dump-about-frame`: the tagline is now sharp and fully legible at a
    glance, not just "technically bigger."

- **PL/EQ button fill (TODO: "the PL and EQ buttons are ugly ... uniform
  with the same green instead of black where the letters are").** Two
  separate bugs stacked on top of each other, both fixed:
  - **Root cause of "black where the letters are"**: `RenderTextTexture`
    built every text texture on an *opaque black* canvas (`for (i=3;
    ...) canvas.rgba[i]=255`), and `BitmapFont::BlitCell` unconditionally
    copies all 4 bytes from the glyph atlas - including its "off" pixels,
    which are pure `(0,0,0)` baked into `display_font.bmp` (confirmed by
    sampling the atlas directly: the 'P' glyph's off-pixels really are
    `(0,0,0)`, on-pixels `(0,255,0)`). So *every* text draw, anywhere in
    the app, always punched an opaque black rectangle through whatever
    was underneath it - invisible everywhere else (already-black
    backgrounds), but exactly the visible "black hole" the user was
    describing on the PL/EQ toggle buttons and the EQ preset buttons
    (NORMAL/ROCK/POP/BASS/TREBLE), the only two features that ever drew
    text over a colored fill. Fixed at the source rather than special-
    casing these two callers: `BlitCell` now treats `(0,0,0)` in the
    atlas as a transparent color key (skips the copy instead of writing
    it), and `RenderTextTexture`'s canvas starts fully transparent
    (alpha=0) instead of opaque black - safe everywhere else in the app
    since transparent-over-black looks identical to opaque-black-over-
    black, and correct here since it lets text finally composite over a
    colored fill instead of overwriting it.
  - **The fill colors were also just too dark to read as "green" at
    all** - both the "active" and "inactive" states were near-black
    (`0x14-0x15` range in each channel), which is *why* the black text
    canvas bug went unnoticed as a separate issue for so long: even a
    fully-fixed transparent text draw over a near-black fill still looks
    almost black. Rebalanced both `drawToggle` (Main's PL/EQ toggle
    buttons) and the EQ preset-button loop to a real, clearly-green
    palette: inactive fill `(0x1a,0x5a,0x2a)`, active fill
    `(0x2a,0x8f,0x46)`, borders brightened to match. Confirmed via
    `--dump-frame`/`--dump-eq-frame` crops: PL/EQ now read as solid green
    boxes with the label sitting directly on the fill (no black box
    behind it), and `--eq-preset rock` confirmed the active/inactive
    distinction still reads clearly (brighter fill + bright border on the
    selected preset) despite neither state being anywhere near black
    anymore. Full 11-test suite green throughout (including
    `font_render_test`, which exercises the changed `BlitCell` path
    directly - unaffected since its own test canvas already starts
    opaque black, so color-keying black-over-black is a no-op there).

- **EQ state now saved/resumed (TODO: "EQ mode not saved").** Previously
  `app::Settings` covered specMode/volume/playing/index/xSound but never
  touched the equalizer at all - the EQ silently reset to flat on every
  relaunch. Added two fields: `eqPreset` (mirrors `eqCurrentPreset` in
  main.cpp - which preset button, if any, should stay highlighted; -1 =
  none/manual) and `eqBands` (the actual 10 per-band gains, -127..127).
  Both are saved separately and deliberately, not derived from each
  other: a manual tweak *after* picking a preset needs the exact tweaked
  values restored, not snapped back to that preset's fixed table, so
  `eqBands` is the source of truth for actual audio behavior and
  `eqPreset` is purely a UI-highlight nicety layered on top - same
  "functional value first, UI nuance too" split volume/xSound already
  used. New `EQPRESET=`/`EQBANDS=` (comma-separated) keys in
  `session.cpp`'s serializer; `EQBANDS` tolerates fewer fields than
  expected (fills what's present, leaves the rest at their struct
  defaults) rather than rejecting the whole line, same tolerant spirit as
  every other key there. Restored alongside volume/xSound in the existing
  `if (resumeSession) { engine.SetVolume(...); engine.SetXSound(...); ...
  }` block; `eqCurrentPreset`'s own initializer now reads
  `sessionSettings.eqPreset` under the same `resumeSession` gate rather
  than always starting at -1. `tests/session_test.cpp` extended with a
  round-trip check (preset + all 10 bands) and a short-EQBANDS tolerance
  check. Verified for real, not just unit-tested: ran the actual built
  binary through `--eq-preset rock --eq-band 2 55 --auto-advance-test
  0.2` against an isolated `HOME`, confirmed `settings.cfg` came out with
  `EQPRESET=1` and the full 10-value ROCK band table; a second real
  process pointed at that same `HOME` printed `Resumed session: ...
  eqPreset=1` on the existing session-resume cout line, confirming the
  save/load round-trip through the real files, not just the pure
  serializer. Repeated with a manual-only tweak (no preset) to confirm
  `eqPreset=-1` round-trips correctly too, distinct from "preset
  selected." Full 11-test suite green, clean rebuild and `.app` bundle
  both confirmed.

- **BitRate/Freq readouts now real (TODO: "the Khz and bit rate labels
  are fake and not displaying what they are supposed to display").**
  Freq ("44K") was already genuinely computed from `engine.sampleRate()`
  when a track is open - but every test fixture in `tests/fixtures/`
  happens to be exactly 44.1kHz/128kbps, coincidentally matching both
  hardcoded placeholders exactly, which is almost certainly why it read
  as "fake" even though only half of it actually was. BitRate ("128")
  really was a `RenderTextTexture` built once at startup and never
  touched again, regardless of what got opened afterward.
  - Added `ComputeAvgBitrateKbps(path, durationSeconds)` (file size /
    duration - exact for CBR, an honest approximation for VBR, same
    caveat the Info window's version of this same math already
    documented; dr_mp3/dr_flac don't expose a true running bitrate the
    way the original's `xmMP3_getPlayBitRate` did). BitRate in `drawFrame`
    now calls this live, same per-frame-rebuilt pattern Duration/Freq
    already used, instead of drawing a texture built once outside the
    frame loop. `drawInfoFrame`'s own bitrate line, previously the same
    calculation duplicated inline, now calls the same helper - no
    behavior change there, just de-duplicated.
  - **Verified with a genuinely different file**, not just re-reading the
    existing 44.1kHz/128kbps fixtures (which would have looked identical
    whether or not the fix actually worked): `ffmpeg`-encoded a fresh
    22.05kHz/64kbps MP3, dumped both the main window and Info window via
    `--dump-frame`/`--dump-info-frame`, and confirmed the readout cluster
    now genuinely reads "71" (the real computed average - close to
    ffmpeg's own reported 70.6kbps) and "22K" (the real sample rate),
    matching the Info window's "71 KBIT/S" / "22050 HZ" lines exactly -
    proof the shared helper produces identical output in both places, and
    that the main window's readout is no longer a static placeholder.
    Full 11-test suite green, clean rebuild and `.app` bundle both
    confirmed.
  - **Follow-up (units)**: user wanted "320k 44kHz" instead of "320
    44K" - both readouts now carry their unit suffix (`%dk`/`%ukHz`
    instead of bare `%d`/`%uK`). Renders as "320K"/"44KHZ" in the actual
    app since the bitmap font only has uppercase glyphs (confirmed via
    `--dump-frame`) - same as every other string in the UI, not a bug.
    Both still fit their 27px field widths at realistic values. Info
    window's already-explicit "Bit Rate (avg): X Kbit/s"/"Frequency: X
    Hz" lines were left alone - the ask was about the compact main-window
    readout specifically.
  - **User then asked for literal "kHz"** (mixed case). Checked the actual
    `display_font.bmp` atlas pixels directly (not just the code) to be
    sure before answering - confirmed it truly has no lowercase glyphs
    anywhere (A-Z/digits/punctuation only, matching the original's own
    DrawChar Select Case, which never handled lowercase either). Presented
    the real tradeoff - keep "KHZ" as-is, or hand-draw 3 new lowercase
    5x6px glyphs and extend the renderer to support mixed case just for
    this one label - user chose to keep "KHZ". No code change; documented
    here so this isn't re-litigated as a bug later.
- **Fixed: native file dialogs (Eject's "Add to Playlist" open dialog,
  the folder-picker, and Playlist's Save dialog) could pop up out of
  focus** - `osascript -e 'choose file/folder/file name ...'` without an
  owning application context sometimes surfaces the panel behind the
  app's own SDL windows instead of in front, since nothing tells macOS
  which process should be made key when the panel appears. Fixed in
  `src/app/file_dialog.cpp`'s three `osascript` command strings
  (`OpenNativeFileDialogFiles`, `OpenNativeFileDialogFolder`,
  `SaveNativeFileDialog`) by wrapping the existing `choose ...` line in
  `tell application "System Events" / activate / delay 0.15 / ... /
  end tell` - `activate`ing System Events before the panel is created
  forces it frontmost, and the short delay gives the activation time to
  land before the panel draws (no delay was sometimes not enough for the
  panel to actually come forward, per hands-on testing). Pure string
  changes to the shelled-out command only - `app::ParseDialogOutput`
  (the tested, pure-parsing half of this file) is untouched, so
  `file_dialog_test`'s 8 checks stay valid as-is. Bumped `kVersion` to
  "1.0.33" (`include/app/version.h`) alongside it. Not yet re-verified
  live on the user's real machine (only via the `try`/`on error` path,
  same limitation noted in "Verification limits" below - a real
  interactive dialog pop-up can't be watched for focus in this sandbox).

## Real crash, found via a user-submitted macOS crash report and fixed

The user pasted a real `.app` bundle crash report (EXC_BAD_ACCESS /
SIGSEGV, `KERN_INVALID_ADDRESS at 0x5f`), not a TODO item - treated as
top priority over the remaining TODO queue. Root cause and fix:

- **The bug**: `Mp3Decoder::totalFrames()` (src/audio/mp3_decoder.cpp)
  called `drmp3_get_pcm_frame_count(&dec_)` on *every single call* - and
  that dr_mp3 function isn't a cheap read, it actually seeks/decodes
  through the file to count frames (MP3 has no reliable frame-count
  header) on the *same* `drmp3` handle the background decode thread
  drives for real playback. `Engine::durationSeconds()` (which calls
  `totalFrames()`) was already being called every render frame from the
  *main* thread - for the seek bar's fraction (`main.cpp`, the
  `seekFrac` calc) and the Info window - and this session's own BitRate
  fix (just above) added one more call site. So: main thread scanning/
  seeking through the shared decoder while the decode thread
  concurrently calls `ReadFrames`/`drmp3_read_pcm_frames_f32` on that
  same handle - a genuine data race, corrupting dr_mp3's internal
  bit-reader/buffer state. The crash report's own stack traces prove
  it directly: **two threads caught simultaneously inside
  `drmp3dec_decode_frame`** - thread 0 (main) via
  `durationSeconds→totalFrames→drmp3_get_pcm_frame_count→...`, thread 14
  (the decode thread) via `DecodeThreadMain→drmp3_read_pcm_frames_f32→
  ...` - both hitting the same non-reentrant decoder at once. (The old
  code's own comment even said "not fine to call in a hot loop" -
  correctly identified the smell, but the call site that violated it
  wasn't caught until this crash actually happened.)
  - Not something this session introduced from scratch (the seek-bar
    call site predates it), but the BitRate fix above did add a second
    per-frame call into the same hazard, and fixing the shared root
    cause matters far more than which call site happened to tip it over.
- **The fix**: compute the frame count **once**, synchronously, inside
  `Mp3Decoder`'s constructor - right after `drmp3_init_file` succeeds,
  which is strictly before `Engine::Open` ever starts the decode thread
  (confirmed by reading `Engine::Open`'s own sequence: `decoder_ =
  OpenDecoder(path)` completes fully before `decodeThread_ =
  std::thread(...)` a dozen lines later) - so nothing else can possibly
  be touching the decoder yet. Cached in a new `totalFrames_` member;
  `totalFrames()` now just returns it - O(1), and safe to call from any
  thread at any time afterward, by construction rather than by careful
  call-site discipline. Also checked dr_mp3's own source
  (`drmp3_get_mp3_and_pcm_frame_count` in `third_party/dr_mp3.h`) to
  confirm it saves `currentPCMFrame` before its scan and seeks back to
  it afterward - calling it at construction time (`currentPCMFrame` is
  always 0 then) correctly leaves the decoder positioned at frame 0 for
  normal playback to start from, not somewhere else in the file.
  `Decoder::totalFrames()`'s own doc comment (`include/audio/decoder.h`)
  now states the "must be cheap and thread-safe to call anytime"
  contract explicitly, with this exact incident as the reason, so a
  future decoder implementation (or FLAC, which already caches via
  dr_flac's own metadata) doesn't reintroduce the same class of bug.
  - **Verified for real**: full 11-test suite still green (including
    `decoder_test`'s exact frame-count assertions and
    `engine_smoke_test`'s duration/position/reopen checks - all
    unchanged, confirming the cached value is identical to what the old
    on-demand scan produced). Then specifically stress-tested the actual
    race scenario the crash report showed: real playback of a 5-second
    fixture via `--auto-advance-test 4.0` (drives the real interactive
    loop, calling `durationSeconds()` multiple times per rendered frame
    from the main thread) while the real decode thread concurrently
    decoded and fed the ring buffer for ~4 real seconds - no crash,
    where the pre-fix code was demonstrably unsafe under exactly this
    condition. Clean rebuild and `.app` bundle both confirmed afterward.

## TODO: "there are visualizations missing from the original" - CardioOSC + idle logo, now ported

**How this was scoped**: the user provided a real screen recording of the
original app (`xmplayer/xmpofold.mov`) cycling through its visualizations.
Extracted frames with `ffmpeg` (1fps contact sheet first, then zoomed
individual frames), which surfaced two things this port was missing
entirely: a static skull/"XmP" branding image, and a `CardioOSC` hover
tooltip over a scattered-dot dual-panel display. Rather than guess at
behavior from the video alone, read the actual VB6 source
(`xmplayer/Source/volume.bas` = `modVisualMusic`, `xmp.frm`) to get the
real mechanism, names, and control wiring - confirmed the video's "xmMP3
Stone Peak"/"CardioOSC" tooltips are literal `ToolTipText` values set in
`DisegaSpectrum`/on the picCardioSin/Des controls, and found the exact
click-handler wiring (`analyzer_Click`/`ImgLogo_Click`/
`picCardioSin_Click`/`picCardioDes_Click`) that ties everything together.

**What was actually missing**: the analyzer's own 6 SpecMode sub-modes
(Peak Falls/Stone Falls/Stone Peak/Fade FFT/Osc/Stereo Osc) were already
fully ported and match `SpectrumeMode`'s enum order exactly - that part
was already complete. What's genuinely new is that in the original, the
analyzer's box is shared by **three** independent top-level displays,
cycled by clicking it, only one of which (the analyzer) existed here:

1. **Analyzer** (unchanged) - the 6 sub-modes above.
2. **Idle logo** (`ImgLogo`, saved as `CPULESS` in the original's INI - a
   deliberately low-CPU "nothing to compute" placeholder). Confirmed by
   reading `xmp.frm`'s `LoadDataIntoFile("LOGOXMPCPRS")` that this is a
   REAL embedded asset, not something to redraw by hand - extracted it
   from the same `xmP.RES` used for SKULL/mode-icon assets earlier this
   session, and it turned out to be a genuine JPEG (not a bitmap like
   every other asset in this app - `LOGOXMPCPRS`'s "CPRS" really does
   mean a compressed format, JFIF/JPEG specifically, 100x45, "ACD Systems
   Digital Imaging" in its own comment field) - decoded with PIL, matches
   the video frame-for-frame (skull + green "XmP" wordmark with small
   gold sparkle dots). Flattened onto the app's window-fill color and
   saved as `assets/skin/idle_logo.bmp` (24bpp, like most other assets -
   the loader has no JPEG support, nor does it need any at runtime).
   Drawn stretched to fill the analyzer's box (`DrawTextureFit`, a new
   scaled-blit helper - every other asset in this app draws at native
   size) - matches `ImgLogo.Stretch = True` in the original.
3. **CardioOSC** (`SpectrumSin`/`SpectrumDes` + `picCardioSin`/
   `picCardioDes`) - a stereo VU-bar + scrolling-oscilloscope display.
   Reading `xmp.frm`'s Form_Load confirmed `SpectrumDes.Height =
   analyzer.Height` / `.Top = analyzer.Top` (etc.) - these controls are
   runtime-repositioned to share the analyzer's exact box, not
   independently placed, so this port positions them the same way
   (derived from `kAnalyzerX/Y/W/H`, not hardcoded twip conversions).
   Two pieces, both reusing existing machinery rather than new assets:
   - **VU bars**: `SpectrumSin`/`SpectrumDes` are a custom compiled OCX
     (`XmP1.SpectrumCtrl`, no source available) but `.Modello(gph.Picture)`
     in `Form_Load` confirms they're just fed the same PICGPH gradient
     bitmap this session's channel-level LED meters already reproduce
     pixel-for-pixel (`gfx::LevelGradientColor`) - so these VU bars are
     literally `DrawGradientBar` (already existed, used for the FFT bars)
     driven by the same `peakL`/`peakR` this port already computes for
     the always-visible L/R peak-meter row. No new gradient/asset work.
   - **Scrolling traces**: `picCardioSin`/`picCardioDes` are driven by
     `DisegnaCardioOSC`'s literal `PSet` calls - one new point plotted
     per update at the current scroll column, nothing else touched,
     until `IndiceOsc` reaches the panel's width and the whole picture
     box is `.Cls()`-ed and restarts from column 0. Ported as two
     persistent `gfx::Image` pixel buffers (`cardioSinBuf`/`cardioDesBuf`,
     real RGBA alpha=0 background - NOT the bitmap-font's black-color-key
     trick, since these are built directly in code rather than loaded
     from an always-opaque BMP) that accumulate one point per `drawFrame`
     call via `plotCardioColumn`, then clear via `std::fill` and reset
     `cardioScrollIdx` to 0 on wraparound - re-uploaded as a texture and
     blitted each frame (small enough, 39x23px, that per-frame
     recreation is cheap; nothing else in this app needed a *persistent*
     pixel buffer before, every other dynamic texture is a fresh text
     canvas rebuilt from scratch each frame).

**Click cycle**: the original wires this as three separate handlers
(`analyzer_Click` -> shows `ImgLogo`; `ImgLogo_Click` -> shows CardioOSC;
`picCardioSin_Click`/`picCardioDes_Click` -> shows `analyzer` again) that
only ever do one thing each - advance to the next panel in a fixed order.
Collapsed into one hit-test on the analyzer's box
(`kAnalyzerX/Y/W/H`) that advances a single `VisPanel{Analyzer, IdleLogo,
CardioOSC}` enum mod 3, rather than porting three separate click handlers
that would all reduce to the same one-liner anyway.

**Also ported**: `CommandImg(9).Enabled = analyzer.Visible` - the
SpecMode button only cycles `VisMode` while `VisPanel::Analyzer` is
actually showing (a real, findable line in `xmp.frm`, not inferred) -
`handleUtilityPress`'s SpecMode branch is now gated on `visPanel ==
VisPanel::Analyzer`, a no-op otherwise rather than disabling/graying the
button itself (no new asset for a disabled-button appearance was
introduced - out of scope for what was asked).

**Session persistence**: new `visPanel` field in `app::Settings`
(0=Analyzer/1=IdleLogo/2=CardioOSC), mirroring the original's own
ANALYZER/CPULESS/COSC INI flags collapsed into one field (exactly one is
ever true at a time in practice). `tests/session_test.cpp` extended with
round-trip + missing-key-default coverage.

**Verified for real, several ways**:
- `--sim-click main 50 80` (the analyzer's real on-screen coordinates,
  run through the actual `inRect` hit-test in `processEvent` - not a
  debug shortcut) three times in a row correctly cycled `visPanel` 0 ->
  1 -> 2 -> 0 (confirmed via a new `visPanel=`/`visMode=` field added to
  the existing sim-click diagnostic line).
- `--dump-frame` after 0, 1, and 2 clicks: analyzer bars unchanged, idle
  logo frame shows the real skull/XmP JPEG rendered correctly, CardioOSC
  frame shows both VU bars and scroll-trace dots (very sparse with only
  one `drawFrame()` call, as expected).
- `--vis-ticks 30` (steps the render loop without presenting, reusing an
  existing debug flag from the oscilloscope work) before dumping
  CardioOSC: confirms the scroll trace actually accumulates multiple
  points over time, forming a real trace, not just a single dot -
  necessarily flat/constant-height for this particular sine-tone test
  fixture (constant amplitude), unlike the video's real-music footage,
  but the *mechanism* (one PSet per tick) is directly verified.
- `--vis-ticks 85` (more than double the ~39px trace width): confirmed
  the buffer actually clears and restarts at the boundary rather than
  growing unbounded or corrupting - only a short partial trace remained,
  matching `85 mod ~39` columns since the last wrap.
- SpecMode gate, both directions: `--sim-click main 50 80` (-> IdleLogo)
  then `--sim-click main 175 92` (the real SpecMode button coordinates)
  left `visMode=0` unchanged; the same SpecMode click with no prior panel
  switch (still on Analyzer) changed `visMode` 0 -> 1. Proves the gate
  reads the real click path, not just the debug `--specmode-clicks`
  shortcut (which - caught in the process - runs before `--sim-click` in
  source order regardless of CLI argument order, a debug-hook ordering
  quirk worth remembering for future tests: hooks fire in a fixed
  internal order, not the order they're typed on the command line).
- `visPanel=2` round-tripped through a real save/resume cycle (inspected
  the actual `settings.cfg` contents, then a second real process printed
  `Resumed session: ... visPanel=2` from that file).
- Bundle check: `build/xmad.app`'s `Contents/Resources/skin/` picked up
  `idle_logo.bmp` automatically via the existing `cp -R assets/skin` step
  (no `make app` changes needed), and a zero-arg bundle launch + sim-click
  produced pixel-identical output to the dev binary.
- Full 11-test suite green throughout, clean rebuild and `.app` bundle
  both confirmed.

**Not ported / deliberately out of scope**: `frmVisualizzazioni` (a
separate popup window `DisgnaxmFFT` mentions loading/unloading, gated on
a `frmVisualizzazioniAttivo` flag with a comment about mouse-wheel
interaction) - no evidence in the video that this window ever actually
appears, and the user's ask was specifically about what the video shows
cycling through, which is fully covered by the three panels above.

**Follow-up: visualization tooltips** ("put tooltips on the
visualizations so I can tell you which ones need refining"). No native
tooltip widget exists in this SDL app, so this is a small drawn box that
follows the cursor while it's over the analyzer's box - shown
immediately, no hover delay. Labels are the real `ToolTipText` values
from the original wherever one exists (`DisegaSpectrum` sets
`xmp.analyzer.ToolTipText` per sub-mode - "xmMP3 Stone Peak
Falls"/"xmMP3 Stone Falls"/"xmMP3 Stone Peak"/"xmMP3 Fade FFT"/"Osc"/
"Stereo Osc"; `picCardioSin`/`picCardioDes`'s is a fixed "CardioOSC" in
`xmp.frm`) - "Idle Logo" for `VisPanel::IdleLogo` is this port's own
label, called out as such in the code comment, since `ImgLogo` has no
`ToolTipText` in the original at all.
- New `mainMouseX`/`mainMouseY` state (-1,-1 = not hovering), updated on
  `SDL_MOUSEMOTION` for the main window and cleared on
  `SDL_WINDOWEVENT_LEAVE` so a stale tooltip can't linger after the
  cursor leaves. Tooltip box position is clamped to stay fully inside the
  window regardless of where in the analyzer box the cursor is.
- New `--hover <x> <y>` debug flag - builds a real `SDL_MOUSEMOTION` and
  runs it through the actual `processEvent`, same principle as
  `--sim-click`, so this can be verified against the real event path
  rather than by poking the state variables directly.
- **Verified for real**: `--hover 50 80` (inside the analyzer box) at
  each of the three `VisPanel` states plus a `--specmode-clicks 4`
  (Oscilloscope) case - `--dump-frame` confirmed all four labels render
  correctly ("XMMP3 STONE PEAK FALLS", "IDLE LOGO", "CARDIOOSC", "OSC").
  Also checked a near-edge hover position (`--hover 100 92`, close to the
  analyzer's bottom-right corner) to confirm the clamp keeps the box
  fully on-screen rather than drawing off the window edge. Full 11-test
  suite green, clean rebuild and `.app` bundle both confirmed.

**Follow-up ("Idle Logo is squished")**: the first version stretched the
logo's real ~100x45 aspect ratio into the analyzer's own 89x23 box
(`DrawTextureFit(..., kAnalyzerX, kAnalyzerY, kAnalyzerW, kAnalyzerH)`),
flattening it noticeably - the original's `ImgLogo` control is actually a
separate, TALLER box (`Height=600 twips=40px`, not the analyzer's 23px).
Fixed by filling the analyzer's width (89px, keeping the same horizontal
footprint/click hit-box as the other two panels) and deriving height from
the logo's own native aspect ratio instead: `89 * 45/100 ≈ 40px` - which
lands almost exactly on the original's real 40px design value, not a
coincidence so much as confirmation this is the right approach - then
vertically centering that taller box on the analyzer's own center,
letting it extend slightly above/below like the original's box does.
Checked there's actually room for the overflow before relying on it
(nothing else occupies that x-range between the volume slider above,
`kVolSliderY=61`, and the transport row below, `kTransportY=104`).
Confirmed via `--dump-frame` and a zoomed crop: skull and "XmP" wordmark
now have correct, undistorted proportions, no overlap with either
neighboring element. Full 11-test suite green, clean rebuild and `.app`
bundle both confirmed.

**Follow-up ("Fade FFT. The bars need to be larger, enough to fill the
difference with OSC")**: FadeFft was reusing the exact same spaced 2px-
bar-plus-gap geometry (`Gap=2/Barwidth=1`) as the other three bar modes
(PeakFalls/NoPeakFalls/PeakNoFalls), which read as visibly thinner/
smaller than the continuous, full-width OSC line right next to it in the
same SpecMode cycle. Re-checked the real source for this specific mode
(volume.bas's `DisgnaxmMP3FFT`, not `BitBltSpec` - FadeFft is the one
sub-mode that's genuinely a different renderer, not just a ballistics
variant) and confirmed it iterates every single pixel column across the
full `analyzer.ScaleWidth`, not spaced bars at all - so the previous
spaced geometry was actually a divergence from the original for this
mode specifically, not just a stylistic choice. Fixed by giving FadeFft
its own dense, gapless column layout: widths computed so `kAnalyzerW`
(89px) distributes evenly across `kNumBars` (27) with no gaps and no
edge overflow (`bx = kAnalyzerX + i*kAnalyzerW/kNumBars`, width = next
boundary minus this one - not just widening the fixed 2px bars, which
would've left a leftover gap at the right edge since 89 isn't evenly
divisible by 27). The other three bar modes are untouched - same 2px+gap
geometry as before, confirmed via a fresh `--dump-frame` crop showing
PeakFalls' bars and blue peak dashes unchanged. FadeFft's own crop now
shows contiguous, touching columns with no gaps, visibly larger/denser
than before and much closer in visual weight to OSC's continuous line.
Full 11-test suite green, clean rebuild and `.app` bundle both confirmed.

**Follow-up ("check the other visualizations too")**: re-read every
remaining mode's real source line-by-line against this port's code
looking for the same class of divergence the FadeFft fix caught, rather
than just eyeballing them.
- **PeakFalls/NoPeakFalls/PeakNoFalls (`BitBltSpec`)**: re-derived the
  ballistics by hand (the original tracks Y as *distance from the top*,
  this port tracks height from the bottom - inverted coordinate systems)
  and confirmed the instant-rise/eased-fall bar logic and the peak-hold
  snap/creep logic both translate correctly as already implemented - no
  bug found here.
- **Found and fixed a real one: mono "Osc" was silently dropping the
  right channel entirely.** `ShowMovingLine` in the original explicitly
  mixes `(waveTblL(I) + waveTblR(I)) / 2` for the single-channel display;
  this port's `drawChannel(0, ...)` only ever read channel 0 (left).
  For any stereo file where L and R genuinely differ - anything panned,
  or even this port's own XSound stereo-widen effect - the mono
  Oscilloscope was showing the left channel alone and completely
  ignoring the right one. Fixed by refactoring `drawChannel` into a
  `drawWave(originX, width, sampleAt)` that takes a per-sample lookup
  function: stereo mode still passes a single channel per side
  (unchanged), mono mode now passes an averaging lookup matching the
  original's exact mix. **Verified with a purpose-built test file**, not
  just re-checked the existing fixtures (which are all near-identical
  L/R content, so this bug wouldn't have been visible in them at all):
  `ffmpeg`-generated a stereo MP3 with true digital silence on the left
  and a 440Hz tone on the right (confirmed via a standalone throwaway
  program linking this project's own `Decoder` directly - left RMS
  0.0, right RMS 0.086 - the bug was never in decoding, only in which
  channel(s) the drawing code read). At default 25% volume the wave was
  genuinely sub-pixel and looked flat in both old and new code (a
  measurement red herring, not a re-appearance of the bug); at
  `--set-volume 100` the mono Osc line clearly traced the right
  channel's waveform post-fix, and StereoOsc correctly showed left flat/
  right waving side by side, proving per-channel separation survived
  the averaging change for the mode that should keep it.
- **CardioOSC/idle logo**: re-checked against `DisegnaCardioOSC`/
  `SetCardioOSCVisibile`/`ImgLogo` handling from the work earlier this
  session - no further divergences found beyond the aspect-ratio fix
  already applied.
- **Noted, not changed**: `BitBltSpec` clamps its Y position so a bar
  never quite reaches true silence (`Tolleranza=1` - always at least a
  1px-tall sliver, even at zero signal); this port lets bar height decay
  all the way to 0px. Minor and not raised as a complaint - flagging it
  here in case it's ever worth matching exactly, not fixing preemptively.
- Full 11-test suite green, clean rebuild and `.app` bundle both
  confirmed after the Oscilloscope fix.

**Follow-up ("Stone peak fall and peak fall don't fill in the entire
space, make them larger to match OSC")**: confirmed via `AskUserQuestion`
which of the three similarly-named bar variants this covered - all
three ("Stone Peak Falls"/"Stone Falls"/"Stone Peak" -
PeakFalls/NoPeakFalls/PeakNoFalls). Applied the exact same dense/gapless
column geometry FadeFft got earlier to these three as well - they all
share one code path (only `showPeak`/`fallSpeed` differ per mode), so
this was a small generalization: dropped the `denseFft`
mode-check entirely and made the evenly-distributed, gapless bx/barW
computation unconditional for every bar mode. Explicitly noted in the
code comment that this is different from the FadeFft case: FadeFft's
dense fill matches what the real `DisgnaxmMP3FFT` renderer actually
does, but the real `BitBltSpec` (which these three modes port) genuinely
is sparse-barred in the original - so widening these three is a
deliberate user-requested style choice for consistency with OSC, not a
fidelity correction. Verified via `--dump-frame` crops of all four bar
modes (0-3 SpecMode clicks): Stone Peak Falls, Stone Falls, and Stone
Peak all now show contiguous full-width columns with their own correct
peak-dash/no-peak-dash behavior and color preserved; FadeFft re-checked
unaffected by the refactor. Full 11-test suite green, clean rebuild and
`.app` bundle both confirmed.

**Follow-up ("I want to see a small gap between the bars with those
though")**: "those" = the three just-widened Stone modes specifically,
not FadeFft (which stays fully gapless - matches the real
`DisgnaxmMP3FFT` per-pixel fill, no reason to add an artificial gap to
the one mode where gapless is actually faithful). Added a 1px
`kBarGapPx`, subtracted from each mode's already-evenly-distributed
segment width (`std::max(1, segW - gap)` - never lets a bar vanish
entirely at the smallest segment widths), gated on `visMode !=
FadeFft`. Verified via `--dump-frame` crops of all four bar modes at
higher zoom than before specifically to make a 1px gap legible: Stone
Peak Falls/Stone Falls/Stone Peak all show a clean visible gap between
otherwise-large bars, FadeFft confirmed still fully contiguous. Full
11-test suite green, clean rebuild and `.app` bundle both confirmed.

**Follow-up ("The bar are of differente size, make them all the same
size. If it doesn't divide evenly it's okay to live some black pixels at
the left")**: the previous evenly-distributed segment widths
(`kAnalyzerX + (i*kAnalyzerW)/kNumBars`-style) varied by 1px between
bars (3 vs 4px) to spread `kAnalyzerW`'s remainder invisibly across all
of them - exactly the "different size" the user was seeing. Replaced
with a fixed `kBarSegW = kAnalyzerW / kNumBars` (floor, so always 3px
here) for every bar, and a `kBarRowX0` starting point that pushes the
entire row right by the full remainder (`kAnalyzerW - kBarSegW*kNumBars`
= 8px here) rather than distributing it - so those unused pixels show
up exactly where asked, as black space at the left edge of the analyzer
box, before the first bar. The 1px gap (previous follow-up) and
FadeFft's gapless exception both still apply, now subtracted from a
uniform segment width instead of a variable one. **Verified precisely,
not just by eye**: scanned actual output pixels column-by-column across
the analyzer's height in the dumped frame - confirmed native x=16..23
(the 8px remainder) are unlit for the entire height in every mode, x=24
is exactly where the first bar starts, and every lit bar segment
thereafter is exactly 2px wide (3px `kBarSegW` minus the 1px gap) with
1px gaps between - for FadeFft, confirmed the same x=24 start with lit
pixels fully contiguous from there (no gap subtracted). Full 11-test
suite green, clean rebuild and `.app` bundle both confirmed.

## TODO: "It uses a lot of CPU even when it's not doing anything" - fixed

Diagnosed against the user's real, actually-running instance rather than
guessed at (this sandbox has no real display, so this one had to lean on
the user directly): `ps aux` showed **84.6% CPU** at idle (track paused,
nothing being interacted with), and a live 5-second `sample` profile of
that process showed the main thread spending essentially 100% of its time
inside SDL2's software renderer (`SW_RunCommandQueue`/`SDL_SoftBlit`/blit
routines). Root cause: the top-level event loop (`while (running && ...)`,
main.cpp's tail) called `SDL_PollEvent` (non-blocking) then unconditionally
redrew and presented all 5 windows (main/playlist/EQ/info/about) every
single iteration, with no `SDL_Delay`, no vsync, and no frame-rate cap
anywhere - all 5 renderers are `SDL_RENDERER_SOFTWARE`, so nothing
throttled it externally either. It simply spun as fast as the CPU allowed,
forever, independent of playback state. (`SESSION_STATUS.md` had
independently flagged this same loop as "unbounded" in a debug-test context
before this was ever traced to the general-CPU complaint - see the
minimize-cascade entry above.)

Fixed in three layered passes, each verified against the user's live,
running instance before moving to the next (this section's own numbers
predicted "low single digits" twice and were wrong both times - real
measurement each step was the only way to know what was actually left):

1. **Frame-rate cap.** Added `SDL_GetTicks()`+`SDL_Delay()` bracketing the
   loop body to cap it at 60fps, plus skipping the draw+present pair
   entirely for any window that's hidden or minimized (`plUserVisible`/
   `eqUserVisible`/`infoUserVisible`/`aboutUserVisible` and the matching
   `*Minimized` bools already existed for the minimize-cascade feature but
   were never consulted before drawing - Info and About are hidden by
   default, so this alone stopped 2 of 5 windows from ever needing to
   render). Took CPU from 84.6% to ~30% - matches expectations: at 60fps
   most of each 16ms budget is spent asleep in `SDL_Delay`, but real
   per-frame render cost across the visible windows turned out higher than
   "a few ms." Note: the fall/decay bar-visualizer animations
   (`kFallsVelSlow` etc.) are tuned in pixels-per-redraw-tick, not
   delta-time, so this incidentally also fixed their speed being
   machine/CPU-load-dependent rather than a fixed, correct rate.

2. **Text-texture caching.** `RenderTextTexture` (rasterize + upload a
   fresh `SDL_Texture`) was being called for every on-screen text field
   every single frame regardless of whether the string had changed -
   marquee/duration/freq/volume/bitrate/PL-EQ-toggle-labels/vis-mode
   tooltip in `drawFrame`, one per visible row plus two status lines in
   `drawPlaylistFrame`, and the EQ's title/legend/10 band labels/5 preset
   names (all of which are permanently static - never depend on any
   runtime value) in `drawEqFrame`, plus similarly in `drawInfoFrame`/
   `drawAboutFrame`. Added a small `CachedTextTexture` helper (keeps the
   texture alive, rebuilds only when text or field width actually differs
   from last call) and wired it into every one of those call sites.
   **Did not reduce CPU at all** (still ~30%, paused) - a live re-profile
   showed the actual bottleneck was elsewhere: the non-text blits (bevel,
   button icons, sliders - never cached) plus `SDL_RenderPresent` itself,
   which on this platform re-uploads the *entire* window surface to a
   Metal-backed texture on every single call regardless of whether any
   pixel changed. Kept anyway (real, if smaller, savings once combined
   with pass 3; no reason to revert a correct, low-risk change) and moved
   on to the actual bottleneck.

3. **Per-window dirty-check.** Since `SDL_RenderPresent`'s fixed
   Metal-texture-upload cost can only be avoided by not calling it at all,
   added a snapshot/diff: `PlaylistFrameKey`/`EqFrameKey`/`InfoFrameKey`
   structs capture everything each window's frame actually depends on
   (playlist generation + currentIndex/selected/scrollOffset/pressedButton
   + sampleRate/channels for Playlist; all 10 `EqBand` values +
   pressedSlider/currentPreset for EQ; playlist generation + currentIndex +
   channels/sampleRate/durationSeconds for Info), compared against the
   previous frame's snapshot in the main loop - the whole draw+present pair
   is skipped outright when nothing changed. Deliberately built as an
   input snapshot rather than scattering `xDirty = true` flags across every
   mutation site in main.cpp (add/delete/reorder/select/drag/etc.): far
   less surface area to get wrong, since it only requires auditing what
   each `draw*Frame` actually *reads*, not hunting down every place that
   could write to it. Needed one small supporting change:
   `Playlist::Generation()`, a counter bumped by every mutating method
   (`Add`/`Clear`/`RemoveAt`/`MoveUp`/`MoveDown`/`LoadM3U`), since
   `MoveUp`/`MoveDown` reorder the list without changing `size()` or
   `currentIndex()`. About gets no key at all - it's purely static
   branding (confirmed via its own existing code comment), so it now draws
   exactly once, ever, per launch. Main is intentionally excluded from this
   entirely: its spectrum/VU meters/marquee are genuinely animated every
   frame during playback, so it keeps redrawing at the full 60fps cap.
   Took CPU from ~30% to **~10.3%**, confirmed via a final live `sample`:
   92% of main-thread samples now land in `SDL_Delay`'s `nanosleep`, with
   the remaining ~8% being exactly Main's own per-frame redraw (expected,
   not waste). **Verified interactively by the user** afterward (the real
   risk of a snapshot/diff approach is missing an input): clicking a
   different playlist row, dragging an EQ slider, and switching tracks all
   still updated on screen immediately - confirmed "all good, works fine."

Full 11-test suite green, clean rebuild and `.app` bundle confirmed after
every pass.

## Known, explicitly-flagged divergences from the original (not bugs)

- Playlist persistence: in-memory + M3U, not the original's per-entry INI
  temp-file mechanism.
- Auto-advance wraps at the end of the playlist; the original's `PlayDone`
  stops at the end unless Repeat/Random is on — neither Repeat nor Random/
  Shuffle is implemented yet, so wrapping was chosen as the more useful
  default. Flagged to the user as a possible future point of divergence.
- Playlist scrollbar thumb/up-down-arrows not implemented (mouse-wheel only).
- ID3 tag *editing* not implemented (reading the title is, as of this
  session - see above).
- A small "mountain icon" visualization mode hinted at in one reference
  screenshot was never identified/implemented (uncertain what it even is).
- `LitePic` (the upward-triangle icon next to Minimize) repurposed as
  "Maximize" (show/hide EQ+Playlist together) at the user's request; the
  original used it to toggle the Windows systray icon, which doesn't
  apply here (see "Minimize now cascades..." above).

## Build & test

```sh
cd xmad-revival
make test          # builds + runs all 11 test binaries
make build/xmad     # builds the app
./build/xmad assets/skin tests/fixtures/track_a.mp3 tests/fixtures/track_b.mp3
```

All 11 tests currently pass: `fft_smoke_test`, `font_render_test`,
`decoder_test`, `engine_smoke_test`, `eq_test`, `window_snap_test`,
`file_dialog_test`, `session_test`, `stereo_widen_test`, `tags_test`,
`level_meter_test`.

Useful debug CLI flags on `build/xmad` (all drive real production code paths,
not reimplementations): `--dump-frame/--dump-playlist-frame/--dump-eq-frame
<path>`, `--click <back|play|stop|next|pause|eject>`, `--playlist-click
<clear|delete|save|up|down|seek>`, `--drop <path>`, `--select-row N`,
`--eq-preset <name>`, `--eq-band N V`, `--specmode-clicks N`, `--vis-ticks N`
[`--vis-pause-first`], `--sim-click <win> x y`, `--sim-click-specmode N`,
`--playlist <m3u>`, `--auto-advance-test <seconds>`, `--clear-reload-test
<path>`, `--set-volume <0..100>`, `--click mute`, `--click info`,
`--dump-info-frame <path>`, `--toggle-xsound <N>`, `--click maximize`,
`--test-minimize-cascade`.

## Verification limits of this sandbox (be upfront about these)

- No real display attached — SDL's accelerated renderer produces blank
  output here; software renderer + `SDL_RenderReadPixels` dump is the only
  way to check pixels headlessly.
- `screencapture` fails (no screen-recording permission).
- Can't visually confirm actual on-screen crispness, real click/focus
  behavior, or real drag-and-drop — those need the user's real machine.
  Where I couldn't verify directly, I've said so rather than asserting it.
- A live `osascript`-based file dialog was test-invoked once this session
  with a background-process + kill safety net (confirmed it pops a real
  panel and blocks correctly) rather than left to run unattended.
- **Real footgun, hit once, now fixed**: any `./build/xmad` invocation with
  no positional track, no `--playlist`/`--play`, and no `--dump-*-frame`
  flag silently engages session persistence's *real* resume/save path
  against the actual `~/.xmad-revival` on whatever machine runs it - there
  is no sandboxing of that by default. Running `./build/xmad assets/skin
  --playlist-click clear ...` (no track args) this session did exactly
  that and clobbered the user's real saved `session.m3u` down to an empty
  playlist; caught via the system's file-changed diff and fixed by hand
  (restoring the playlist entry from the last known-good state). **Always
  override `HOME` (e.g. `HOME=/tmp/scratch-dir ./build/xmad ...`) for any
  test invocation that doesn't already carry an explicit track/playlist/
  dump-frame argument** - those three are the only things that gate the
  resume path off (see `resumeSession` in main.cpp).

## Not yet requested / not started

- Native file-open dialog was the last "not started" item — now done (Eject).
- Repeat / Shuffle (Random) modes — not implemented; relevant to the
  auto-advance wraparound divergence noted above.
- Playlist scrollbar, ID3 tag *editing* — flagged but not requested yet
  (row duration and title-if-present *are* both done now, see above).

## How to resume

Just point me at this file, or at `TODO` (a running list the user adds to
directly - work through it one line at a time, confirming each before
starting, per their instruction). Nothing is mid-edit right now - the
playlist row-duration feature is complete, tested, and the full 9-test
suite is green.
