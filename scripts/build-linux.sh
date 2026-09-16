#!/usr/bin/env bash
# Build and package X.MaD Player Revival for Linux: checks/installs the
# required toolchain, runs the full test suite, and produces a distributable
# .tar.gz under build/.
set -euo pipefail

cd "$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

echo "==> Checking build dependencies..."

PKG_MANAGER=""
if command -v apt-get >/dev/null 2>&1; then
    PKG_MANAGER="apt"
elif command -v dnf >/dev/null 2>&1; then
    PKG_MANAGER="dnf"
elif command -v pacman >/dev/null 2>&1; then
    PKG_MANAGER="pacman"
fi

install_packages() {
    case "$PKG_MANAGER" in
        apt)
            sudo apt-get update
            sudo apt-get install -y build-essential pkg-config libsdl2-dev
            ;;
        dnf)
            sudo dnf install -y gcc-c++ make pkgconfig SDL2-devel
            ;;
        pacman)
            sudo pacman -Sy --needed --noconfirm base-devel pkgconf sdl2
            ;;
        *)
            echo "error: could not detect a supported package manager (apt/dnf/pacman)." >&2
            echo "Please install a C++20 compiler, make, pkg-config, and the SDL2" >&2
            echo "development headers manually, then re-run this script." >&2
            exit 1
            ;;
    esac
}

find_compiler() {
    if command -v clang++ >/dev/null 2>&1; then
        echo "clang++"
    elif command -v g++ >/dev/null 2>&1; then
        echo "g++"
    fi
}

CXX_BIN="$(find_compiler)"
missing=0
[ -n "$CXX_BIN" ] || missing=1
command -v make >/dev/null 2>&1 || missing=1
command -v pkg-config >/dev/null 2>&1 || missing=1
pkg-config --exists sdl2 2>/dev/null || missing=1

if [ "$missing" -ne 0 ]; then
    echo "==> Missing build dependencies detected, installing..."
    install_packages
    CXX_BIN="$(find_compiler)"
fi

if [ -z "$CXX_BIN" ]; then
    echo "error: no C++ compiler (clang++ or g++) found after installing dependencies." >&2
    exit 1
fi

echo "==> Building and packaging X.MaD Player Revival (make tarball, CXX=$CXX_BIN)..."
make tarball CXX="$CXX_BIN"

echo "==> Done. Output:"
ls -1 build/*.tar.gz
