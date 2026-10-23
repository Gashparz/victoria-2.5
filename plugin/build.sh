#!/usr/bin/env sh
# Builds TGCConvertPop.dll (32-bit) with llvm-mingw: https://github.com/mstorsjo/llvm-mingw/releases
# Linux/macOS: put llvm-mingw/bin on PATH and run ./build.sh   (Windows: build.cmd)
set -e
cd "$(dirname "$0")"
i686-w64-mingw32-clang++ -std=c++20 -O2 -Wall -Wextra -shared -static -Iinclude \
  TGCConvertPop.cpp -o TGCConvertPop.dll -Wl,--kill-at
echo "built plugin/TGCConvertPop.dll"
