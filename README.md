I made [X-MaD MP3 Player](https://github.com/ddavini/xmp) (aka XmP) probably 15 years ago because I wanted an MP3 player that worked the way I wanted to. Sadly I used VB for the UI and I stopped using windows completely at some point 10 years ago. I always missed it.

This project is a recoding from scratch using the original assets and source code as base. I built this to use myself
and as a sort of experiment to see how bad/good AI is at these kinds of
things.

TL;DR: I stopped being a developer a while ago so for this project I used Claude
Code, out of curiosity and because I wanted to have xmp on macOS.

## Formats

MP3 and FLAC, plus tracker modules: ProTracker/Soundtracker MOD (including
the original 15-instrument Soundtracker format), FastTracker II XM and
Scream Tracker 3 S3M - also their compressed `.mdz`/`.xmz`/`.s3z`
variants, whether zipped (the classic MODPlug kind) or gzipped. Module
playback uses [ibxm-ac](https://github.com/martincameron/micromod)
(BSD-3-Clause, vendored in `third_party/`).

## Building

- macOS: `./scripts/build-macos.sh` - checks for Xcode Command Line Tools,
  `pkg-config` and SDL2 (installing them via Homebrew if missing), runs the
  test suite, and produces `build/X.MaD-Player-Revival-<version>.dmg`.
  Homebrew's `sdl2` is now `sdl2-compat` (SDL2's API on top of SDL3); the
  `.app` bundles both libraries, so it runs on machines without Homebrew.
  zlib comes with macOS.
- Linux: `./scripts/build-linux.sh` - checks for a C++ compiler, `make`,
  `pkg-config`, SDL2 and zlib (installing anything missing via
  apt/dnf/pacman), runs the test suite, and produces
  `build/X.MaD-Player-Revival-<version>-linux-<arch>.tar.gz`.

For iterative development, the underlying `make`, `make test`, and `make run`
targets remain available directly.

## License

GPL-3.0 - see [LICENSE](LICENSE).