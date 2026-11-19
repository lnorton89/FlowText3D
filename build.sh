#!/usr/bin/env bash
# Cross-compiles FlowText3D.scr with MinGW-w64 (Linux/WSL/MSYS2).
# On Windows with MSYS2: pacman -S mingw-w64-x86_64-gcc, then run this in the MINGW64 shell
# (the tool names drop the x86_64-w64-mingw32- prefix there; set CC=gcc WINDRES=windres).
set -euo pipefail
cd "$(dirname "$0")"
CC=${CC:-x86_64-w64-mingw32-gcc}
WINDRES=${WINDRES:-x86_64-w64-mingw32-windres}
mkdir -p dist
$WINDRES -O coff flowtext3d.rc -o dist/flowtext3d.res
$CC -O2 -std=c11 -Wall -Wextra -Wno-cast-function-type -municode -mwindows \
    -o dist/FlowText3D.scr flowtext3d.c dist/flowtext3d.res \
    -static -static-libgcc \
    -lopengl32 -lgdi32 -luser32 -lcomctl32 -lcomdlg32 -ladvapi32 -lshell32 -lwinmm
rm dist/flowtext3d.res
ls -l dist/FlowText3D.scr
