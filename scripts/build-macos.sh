#!/usr/bin/env bash
# Build and package X.MaD Player Revival for macOS: checks/installs the
# required toolchain, runs the full test suite, and produces a distributable
# .dmg under build/.
set -euo pipefail

cd "$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

echo "==> Checking build dependencies..."

if ! xcode-select -p >/dev/null 2>&1; then
    echo "error: Xcode Command Line Tools not found." >&2
    echo "Install them with: xcode-select --install" >&2
    echo "Then re-run this script." >&2
    exit 1
fi

if ! command -v brew >/dev/null 2>&1; then
    echo "error: Homebrew not found (needed to install SDL2)." >&2
    echo "Install it from https://brew.sh, then re-run this script." >&2
    exit 1
fi

# Homebrew's "sdl2" is now sdl2-compat (the SDL2 API running on SDL3);
# `make dmg` bundles both libSDL2 and libSDL3 into the .app, so nothing
# extra is needed here. zlib (for zipped/gzipped tracker modules) ships
# with macOS itself.
if ! command -v pkg-config >/dev/null 2>&1; then
    echo "==> pkg-config not found, installing via Homebrew..."
    brew install pkg-config
fi

if ! pkg-config --exists sdl2 2>/dev/null; then
    echo "==> SDL2 not found, installing via Homebrew..."
    brew install sdl2
fi

echo "==> Building and packaging X.MaD Player Revival (make dmg)..."
make dmg

echo "==> Done. Output:"
ls -1 build/*.dmg
