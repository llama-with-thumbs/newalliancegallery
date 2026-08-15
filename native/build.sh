#!/bin/bash
# Build the native renderer. Same script on the dev box and on the Pi.
#
#   ./build.sh            optimised build -> ./gallery (gallery.exe on Windows)
#   ./build.sh debug      -O0 -g, warnings as they come
set -e
cd "$(dirname "$0")"

MODE="${1:-release}"
if [ "$MODE" = "debug" ]; then OPT="-O0 -g"; else OPT="-O2 -DNDEBUG"; fi
WARN="-Wall -Wextra -Wno-unused-parameter"

case "$(uname -s)" in
  MINGW*|MSYS*|CYGWIN*)
    # msys2 ucrt64 toolchain
    PREFIX="${MSYS_PREFIX:-/c/msys64/ucrt64}"
    OUT=gallery.exe
    g++ -std=c++17 $OPT $WARN -o "$OUT" main.cpp \
        -I"$PREFIX/include/SDL2" -L"$PREFIX/lib" \
        -lmingw32 -lSDL2main -lSDL2 -lopengl32
    ;;
  *)
    OUT=gallery
    # -mcpu=native lets the Pi 5's Cortex-A76 be targeted directly; harmless
    # elsewhere and skipped if the compiler will not take it
    ARCH=""
    if echo 'int main(){}' | g++ -x c++ -mcpu=native -o /dev/null - 2>/dev/null; then
      ARCH="-mcpu=native"
    fi
    g++ -std=c++17 $OPT $ARCH $WARN -o "$OUT" main.cpp \
        $(pkg-config --cflags --libs sdl2) -lm
    ;;
esac

echo "built ./$OUT"
