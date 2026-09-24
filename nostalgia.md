# Tracker Module Support (MOD / XM / S3M) — Design & Status

## Status

**Implemented** (branch `tracker-module-support`, 2026-09-24) and confirmed by
the user playing their real collection (`~/Music/MODS`, 148 files: 127 `.MDZ`,
14 `.XMZ`, 7 `.S3Z`). All 148 decode.

This file started as a feasibility plan. Two of its assumptions turned out to
be wrong once real files were involved; both are called out below
("Corrections to the original plan") so the reasoning isn't lost.

## Context

X.MaD Player Revival originally played MP3 and FLAC only. The request began
as "Amiga MOD support", but the user's collection is `.mdz`/`.xmz`/`.s3z` -
compressed ProTracker MOD, FastTracker II XM and Scream Tracker 3 S3M. XM and
S3M are PC formats, not Amiga ones, so the scope is MOD + XM + S3M, which one
library covers.

The existing `Decoder` abstraction (`include/audio/decoder.h`) was already
format-agnostic, so tracker playback is a third implementation next to MP3
and FLAC, not a special case.

## Library: `ibxm-ac`

Vendored **`ibxm-ac`** - the ANSI C variant of Martin Cameron's `ibxm`, from
the `martincameron/micromod` repo (commit `990a493`, `IBXM_VERSION` 20260303).
BSD-3-Clause; the source files carry no license header, so the repo's
`licence.txt` is vendored alongside as `third_party/ibxm_LICENSE.txt`.

- Plays MOD, XM and S3M from `ibxm.c`/`ibxm.h`; `module_load()` detects the
  format from the file's own header bytes.
- Instance-based: `new_replay()` returns an opaque `struct replay*`, no global
  state.
- `module_load()` copies all sample data into its own allocations, so the
  input buffer can be freed right after loading.
- Output is interleaved stereo `int`s at 16-bit scale, one *tick* per
  `replay_get_audio()` call (length depends on tempo) - the decoder buffers
  ticks and converts to float.
- Compiled as C with warnings off (`-w`) by its own Makefile rule rather than
  patched.

`libxmp-lite`/`libopenmpt` were rejected as far larger than needed and a poor
fit for this Makefile's no-package-manager build.

## Compressed modules: zip *and* gzip (`src/audio/module_file.cpp`)

`.mdz`/`.xmz`/`.s3z` come in two flavors, told apart by their first bytes,
not the extension:

- **ZIP** (`PK\3\4`) - the classic MODPlug-era format, and what all 148 real
  files are. The loader walks the zip's central directory (local headers can
  have zero sizes when the archiver streamed them), picks the first entry
  named `.mod`/`.xm`/`.s3m` - else the largest file, since some archives carry
  a readme - and inflates it with zlib (raw deflate) or copies it if stored.
  Unsupported methods (PKZIP 1.x "implode", deflate64) and encrypted entries
  fail with a clear error instead of feeding garbage to ibxm.
- **gzip** (`1f 8b`) - what some later tools wrote under the same extensions.
  Inflated with zlib.
- Anything else is treated as a raw module, so a plain `.mod` renamed to
  `.mdz` (or the reverse) still plays.

Uses the **system zlib** (`pkg-config zlib`, falling back to `-lz`), not a
vendored copy. Size caps (64 MB compressed and decompressed) guard against
zip/gzip bombs; real modules are a few MB at most.

## 15-instrument Soundtracker MODs

ibxm only reads 31-sample MODs with a tag at offset 1080 (`M.K.` etc.). The
original Soundtracker format (1987-1990) has 15 sample slots and no tag, so
ibxm rejects it - that was `TOCCATA.MDZ` (a 1991 module using `st-01:` disk
samples). `ConvertSoundtracker15()` rewrites such files to the 31-sample
layout before `module_load()`: 16 empty sample headers padded in, `M.K.`
inserted after the order table, pattern and sample data untouched. The format
has no signature, so it's recognized by shape: no known tag, volumes <= 64,
song length 1..128, orders < 64, and the patterns those orders need actually
fit in the file. Anything ibxm already reads passes through unchanged.

Not emulated: the very first Ultimate Soundtracker's own effect numbering
(1 = arpeggio, 2 = pitch bend) and its tempo byte at offset 471. Later
15-sample trackers mostly used the ProTracker effect meanings ibxm assumes.

## Duration & seeking

- **Duration is computed when the file opens**, via
  `replay_calculate_duration()`: it simulates the song row by row (no mixing,
  so it's fast) until the sequence revisits a row it already played, which
  also ends modules that loop forever through a pattern jump. It runs in the
  decoder's constructor, before `Engine` starts the decode thread - the same
  rule `Mp3Decoder` follows for its frame-count scan (see
  `Decoder::totalFrames`). `ReadFrames` stops at that length, so the track
  ends and the playlist advances.
- **Seeking**: `replay_seek()` restarts from the top and fast-forwards whole
  ticks without mixing; the decoder then renders and discards the rest up to
  the exact frame. Seeks match a straight read sample-for-sample (checked by
  `decoder_test`), apart from ibxm's 64-sample crossfade right after a seek.
- Output is fixed at 48000 Hz stereo, with linear interpolation on
  (`kInterpolation` in `tracker_decoder.cpp`; 0 sounds closer to the gritty
  original hardware).

## Files

- `third_party/ibxm.c`, `ibxm.h`, `ibxm_LICENSE.txt` - vendored verbatim.
- `include/audio/module_file.h` / `src/audio/module_file.cpp` - extension
  check, zip/gzip/raw unwrapping, Soundtracker conversion, header title and
  format parsing. Shared by the decoder and `tags.cpp`.
- `include/audio/tracker_decoder.h` / `src/audio/tracker_decoder.cpp` - the
  `Decoder` implementation.
- `tests/fixtures/make_tracker_fixtures.py` - generates every tracker fixture
  (one 64-row song with a known tone); rerun it to regenerate them
  byte-identically.

## Touch points in existing code

- `Makefile` - zlib flags/libs, the C rule for `ibxm.c`, `ibxm.o` added to
  `AUDIO_OBJS`, zlib on every link line that pulls in the audio objects.
- `src/audio/decoder.cpp` - routes tracker extensions to `OpenTrackerDecoder`.
- `src/audio/tags.cpp` - module song name as the track title (tracker files
  have no other tags). Six of the 148 real files have a blank name and fall
  back to the filename, like untagged MP3s.
- `src/app/file_dialog.cpp` - tracker extensions in the macOS and Linux file
  pickers (the Linux filters list uppercase too, since zenity/kdialog match
  case-sensitively and DOS-era files are usually `SONG.MOD`).
- `src/main.cpp` - drag-and-drop and dropped-folder filters; the Info window's
  MP3/FLAC flag became a three-way format (Format "XM (zip)" etc., Decoder
  "ibxm-ac", tags read-only, Bit Rate "n/a"); `ComputeAvgBitrateKbps` returns
  "unknown" for tracker files, so Main's bitrate readout shows the skin's
  placeholder.
- `scripts/build-linux.sh` - installs zlib's development package.

## Corrections to the original plan

1. **"`.mdz`/`.xmz` are gzip"** - wrong for this collection. The plan assumed
   OpenMPT's later gzip convention; every real file was a MODPlug-era ZIP.
   `gzread` passed the ZIP bytes through unchanged, so the playlist showed
   "PK" (the ZIP magic) as the title and ibxm rejected every file. The test
   fixtures had been generated from the same wrong assumption, so they
   couldn't catch it - they now include zip (deflated and stored, one with a
   readme ahead of the module) as well as gzip.
2. **"Ship with unknown duration"** - would have broken playback flow.
   Modules never stop on their own, so without a real length `ReadFrames`
   never runs short, `Engine` never sees end-of-track, and the playlist never
   advances. ibxm's own player and converter both compute the duration up
   front; this does the same.

## Tests

- `decoder_test`: every fixture (`tone.mod/.xm/.s3m`, zipped
  `tone.mdz/.xmz/.s3z`, `tone_gzip.mdz`, 15-sample `tone15.mod`) decodes to
  the expected pitch (middle C of a 4-cycle loop: 8287/8 Hz for MOD, 8363/8 Hz
  for XM/S3M); exact length (368640 frames at 48 kHz); clean end of stream;
  forward/backward/mid-tick seeks match a straight read; a zip using an
  unsupported method fails with a clear error.
- `tags_test`: title parsing for each format, through zip and gzip, picking
  the module over a readme; container detection; the Soundtracker conversion
  produces `tone.mod` byte-for-byte from `tone15.mod` (bar the tempo byte)
  and leaves tagged MOD/XM/S3M and malformed headers untouched.

## Possible follow-ups (not scheduled)

- Ultimate Soundtracker's original effect numbering and tempo byte.
- Amiga-style `mod.songname` filenames (prefix, not extension) aren't
  recognized.
- A stereo-width option: 4-channel MODs are hard-panned left/right like the
  original Amiga hardware.
- Bringing tracker support to `xmp_web`.
