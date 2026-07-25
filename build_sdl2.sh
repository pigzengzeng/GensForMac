#!/bin/sh
# ============================================================================
#  build_sdl2.sh - download & build a STATIC SDL2 into ./vendor/install
#
#  Why: Homebrew's `sdl2` is actually sdl2-compat (an SDL2->SDL3 shim) whose
#  load-time constructor aborts when its dylib is relocated into an .app
#  bundle. Linking a genuine SDL2 statically makes Gens.app fully
#  self-contained with zero external dependencies.
#
#  Result: vendor/install/lib/libSDL2.a + headers + sdl2-config
#  The Makefile picks these up automatically when present.
# ============================================================================
set -e

SDL_VER="2.32.8"
ROOT="$(cd "$(dirname "$0")" && pwd)"
VENDOR="$ROOT/vendor"
SRC="$VENDOR/SDL2-$SDL_VER"
PREFIX="$VENDOR/install"

if [ -f "$PREFIX/lib/libSDL2.a" ]; then
  echo "==> static SDL2 already built: $PREFIX/lib/libSDL2.a"
  exit 0
fi

mkdir -p "$VENDOR"
cd "$VENDOR"

if [ ! -d "$SRC" ]; then
  echo "==> downloading SDL2-$SDL_VER source"
  curl -sL -o SDL2.tar.gz \
    "https://github.com/libsdl-org/SDL/releases/download/release-$SDL_VER/SDL2-$SDL_VER.tar.gz"
  tar xzf SDL2.tar.gz
fi

echo "==> configuring (static, x86_64)"
mkdir -p "$SRC/build"
cd "$SRC/build"
# -DSDL_DYNAMIC_API=0 is REQUIRED for a usable static lib: without it SDL2's
# dynapi renames every symbol to *_REAL and linking against the .a fails.
CC=clang CFLAGS="-arch x86_64 -O2 -DSDL_DYNAMIC_API=0" \
  ../configure --disable-shared --enable-static --prefix="$PREFIX" \
  > configure.log 2>&1 || { tail -20 configure.log; exit 1; }

echo "==> building (this takes a few minutes)"
make -j"$(sysctl -n hw.ncpu)" > build.log 2>&1 || { tail -20 build.log; exit 1; }
make install > install.log 2>&1

echo "==> done: $PREFIX/lib/libSDL2.a"
