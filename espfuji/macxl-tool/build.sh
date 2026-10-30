#!/bin/bash
# build.sh: build ESPFuji XL with Retro68 -> build/ESPFujiXL.bin (MacBinary), .dsk (800K disk), .APPL
set -eu
cd "$(dirname "$0")"
T=${RETRO68_TOOLCHAIN:-/Volumes/RetroDev/src/Retro68-build/toolchain}
export PATH="$T/bin:$PATH"
mkdir -p build
cd build
[ -f CMakeCache.txt ] || cmake .. -DCMAKE_TOOLCHAIN_FILE="$T/m68k-apple-macos/cmake/retro68.toolchain.cmake" >/dev/null
make 2>&1
ls -l ESPFujiXL.bin ESPFujiXL.dsk
