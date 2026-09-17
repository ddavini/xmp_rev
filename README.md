I made [X-MaD MP3 Player](https://github.com/ddavini/xmp) (aka XmP) probably 15 years ago because I wanted an MP3 player that worked the way I wanted to. Sadly I used VB for the UI and I stopped using windows completely at some point 10 years ago. I always missed it.

This project is a recoding from scratch using the original assets and source code as base. I built this to use myself
and as a sort of experiment to see how bad/good AI is at these kinds of
things.

TL;DR: I stopped being a developer a while ago so for this project I used Claude
Code, out of curiosity and because I wanted to have xmp on macOS.

## Building

- macOS: `./scripts/build-macos.sh` - checks for Xcode Command Line Tools and
  SDL2 (installing SDL2 via Homebrew if missing), runs the test suite, and
  produces `build/X.MaD-Player-Revival-<version>.dmg`.
- Linux: `./scripts/build-linux.sh` - checks for a C++ compiler, `make`,
  `pkg-config`, and SDL2 (installing anything missing via apt/dnf/pacman),
  runs the test suite, and produces `build/X.MaD-Player-Revival-<version>-linux-<arch>.tar.gz`.

For iterative development, the underlying `make`, `make test`, and `make run`
targets remain available directly.

## License

GPL-3.0 - see [LICENSE](LICENSE).