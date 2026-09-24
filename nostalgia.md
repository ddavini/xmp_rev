# Tracker Module Support (MOD / XM / S3M) — Feasibility & Implementation Plan

## Context

X.MaD Player Revival currently plays MP3 and FLAC only — there is no tracker/module engine of any kind in the codebase (confirmed by full-text search: no `libxmp`, `openmpt`, `libmodplug`, `mikmod`, or `uade` reference anywhere, and no MOD/ProTracker/Amiga mentions in 84 commits of history). This was initially scoped as "Amiga MOD support," but the user's actual file collection is `.mdz` and `.xmz` — gzip-compressed ProTracker MOD and FastTracker II XM files respectively (OpenMPT's space-saving convention: `.mdz` = gzip(`.mod`), `.xmz` = gzip(`.xm`)). **XM is not an Amiga format** — it's FastTracker II's own PC format (later than, and more elaborate than, ProTracker MOD, though descended from the same tracker lineage) — so the scope genuinely broadened from "Amiga MOD" to "MOD + XM," and S3M (Scream Tracker 3) is included too since it comes from the same library at no extra integration cost.

The existing `Decoder` abstraction (`include/audio/decoder.h`) is already format-agnostic — MP3 and FLAC are just two implementations of it — so this is a clean third implementation, not a special case.

**Feasibility verdict: yes, straightforward.**

## Library choice: `ibxm-ac`

Recommend vendoring **`ibxm-ac`** — the ANSI C variant of Martin Cameron's `ibxm` (from the `martincameron/micromod` project), BSD-3-Clause licensed. Confirmed directly from its header (`ibxm.h`):

- Plays **MOD, XM, and S3M** from one small library (`ibxm.c`/`ibxm.h`), format auto-detected from the file's own header bytes — no per-format branching needed in our code, one `module_load()` call handles all three.
- **Instance-based state**: `new_replay(module, sample_rate, interpolate)` returns an opaque `struct replay*` handle; there is no shared/global state. Multiple `TrackerDecoder` instances can safely coexist.
- Relevant API surface: `module_load(data, size, error_buf)` → `struct module*`; `dispose_module()`; `new_replay()` / `dispose_replay()`; `replay_get_audio(replay, output, muteMask)` → interleaved stereo; `replay_seek(replay, samplePos)`; `replay_calculate_duration(replay)`; `calculate_mix_buf_len(rate)`.
- Same author, same license family as `micromod-c` (the MOD-only variant originally considered), so it fits this project's existing convention (small, permissive, single-purpose C libraries, matching `third_party/dr_mp3.h`/`dr_flac.h`).

`libxmp-lite`/`libopenmpt` were considered and rejected: both are large multi-file (or full build-system-dependent) libraries built for far more formats/accuracy than needed here, and a poor match for this Makefile's `$(wildcard src/*/*.cpp)`-only, no-package-manager build.

## The gzip layer (`.mdz` / `.xmz` / `.s3z`)

Neither `ibxm-ac` nor any tracker-format library decompresses these — `.mdz`/`.xmz`/`.s3z` are just gzip-compressed `.mod`/`.xm`/`.s3m` files. The fix is a small decompression step in front of `module_load()`, not a library concern:

- Link against the **system zlib** (`pkg-config zlib`, present on macOS and virtually every Linux distro — SDL2 is already pulled in via the identical `pkg-config` pattern in the Makefile) rather than vendoring a new dependency. Add `ZLIB_CFLAGS`/`ZLIB_LIBS` to the Makefile alongside the existing `SDL2_CFLAGS`/`SDL2_LIBS` lines.
- On open, if the extension is `.mdz`/`.xmz`/`.s3z`, read the file and run it through `zlib`'s inflate into an in-memory buffer; if it's `.mod`/`.xm`/`.s3m`, read the raw bytes directly. Either way, the result is a plain in-memory byte buffer handed to `module_load()` — decompression and format-parsing are fully decoupled steps.

## Duration & seeking (decided: ship with unknown duration)

- **`totalFrames()` returns `0` for all tracker files in this first pass** — the interface's own documented value for "unknown" (`include/audio/decoder.h`). Module duration isn't well-defined without a full playthrough simulation (tempo/speed effects, and many modules loop forever via a pattern-jump effect), and `Engine::durationSeconds()`/the UI already tolerate `0`.
- Because `ibxm-ac` is instance-based, a later bounded/background duration scan — via `replay_calculate_duration()` on its own disposable `struct replay*`, with a hard iteration/sample cap — would be safe to add as a follow-up without fighting shared global state. Deferred for now.
- **`SeekToFrame(frame)`**: create a fresh `struct replay*` (or call `replay_seek(replay, 0)` if that resets cleanly — confirm against the header once implementing) and advance via `replay_get_audio()` into a scratch buffer, discarding output, until `frame` samples have been produced. `SeekToFrame` is only ever called from the decode thread, so no new thread-safety concern beyond what MP3 already has, and decoding is far faster than real-time.

## Files to add

- `third_party/ibxm.c`, `third_party/ibxm.h` — vendored verbatim, BSD-3-Clause header kept intact.
- `include/audio/tracker_decoder.h` / `src/audio/tracker_decoder.cpp` — the `Decoder` implementation (`OpenTrackerDecoder(path)`), mirroring `mp3_decoder.h`/`.cpp`'s shape. Handles the gzip-vs-raw branch by extension, then hands the resulting buffer to `module_load()` (format auto-detected from content). `channels()` always `2`; fixed output rate (e.g. 48000 Hz) passed to `new_replay()`; `ReadFrames()` converts `ibxm`'s output to interleaved float32; constructor throws `std::runtime_error` on `module_load()` failure, consistent with `OpenDecoder`'s contract.
- `tests/fixtures/*.mod`, `*.xm`, `*.s3m` (+ at least one gzip-wrapped `*.mdz` to exercise the decompression path) — small hand-authored fixtures, one pattern/known tone each, following `decoder_test.cpp`'s existing `CheckFileDecodesToTone` pattern.

## Touch points in existing files

- `Makefile` — add `pkg-config --cflags/--libs zlib` variables and fold into `CXXFLAGS`/link line, same pattern as the existing SDL2 variables. (`AUDIO_OBJS`'s glob already picks up the new `.cpp` automatically — no other Makefile change needed.)
- `src/audio/decoder.cpp` — add a branch for `mod`/`xm`/`s3m`/`mdz`/`xmz`/`s3z` → `OpenTrackerDecoder(path)`.
- `src/app/file_dialog.cpp` — add all six extensions to the macOS `osascript` `of type {...}` list and the Linux zenity/kdialog filter strings.
- `src/main.cpp` — three spots currently hardcoded to `ext == "mp3" || ext == "flac"`:
  - the dropped-file branch (~line 3311)
  - the dropped-folder directory-scan filter (~line 3331)
  - the Info window (~lines 3628–3681), which today threads a binary `isFlac` bool through tag display, the "Format:" line, and the "Decoder:" line — needs generalizing to cover MOD/XM/S3M as a real multi-way format distinction (e.g. an enum), not just another `else if`. Also force "Bit Rate (avg): n/a" for tracker formats.
- `include/audio/tags.h` / `src/audio/tags.cpp` — add a tracker-format branch to `ReadTrackTags`/`ReadTrackTitle`. MOD's title is the first 20 bytes of the file; XM's is bytes 17–37 of its header ("Extended Module: " prefix precedes it); S3M's is bytes 0–28. All three are simple fixed-offset reads — and for the gzip-wrapped variants, needs the same decompress-to-memory step as the decoder before reading the offset. Deliberately not routed through `ibxm` itself, keeping `tags.cpp` decoupled from the new library dependency.
- `assets/icon/Info.plist` — no change needed; no `CFBundleDocumentTypes`/UTI declarations exist for any format today.

## Stereo image (not a blocker, flagged for awareness)

Classic Amiga ProTracker MOD hardware panning is hard-L/R (channels 1&4 left, 2&3 right); XM/S3M have per-channel panning stored in the file itself, which `ibxm-ac` already honors. No extra work needed — this is only a note that MOD playback will sound "hard-panned" by default, matching original hardware behavior. A user-facing stereo-width option remains a possible small follow-up, not needed now.

## Effort estimate

- **Phase 1**: vendoring `ibxm.c`/`.h`, the zlib Makefile addition, `TrackerDecoder`, the `decoder.cpp`/`file_dialog.cpp`/`main.cpp` (3 spots)/`tags.cpp` touch points, fixtures + `decoder_test.cpp` cases for MOD/XM/S3M (compressed and uncompressed). Comparable in scope to the existing FLAC decoder addition, plus the zlib link step. Roughly 1.5–2.5 days.
- **Follow-up, not scheduled**: bounded duration scan (easier than it would've been with the MOD-only library, since `ibxm-ac` is instance-based — no vendored-file patch required, just a capped call to `replay_calculate_duration()` on a disposable instance); configurable stereo panning width.

## Verification

- `make test` must continue to pass, plus new `decoder_test.cpp` cases exercising `.mod`/`.xm`/`.s3m` and at least one gzip-compressed `.mdz` fixture (open, read frames, check output is non-silent at the expected pitch, seek forward and backward).
- `make run` (or the built `.app`/binary) with real `.mdz`/`.xmz` files dropped onto the playlist or opened via the file dialog — confirm playback starts, seek bar behaves sanely with duration shown as unknown/blank, Play/Pause/Stop/seek all work, and the Info window shows the correct format/decoder without crashing on the tag fields.
- Confirm no regression in MP3/FLAC playback (the Info window's format-enum refactor touches shared code).
