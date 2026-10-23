@echo off
rem Builds TGCConvertPop.dll (32-bit) with llvm-mingw (https://github.com/mstorsjo/llvm-mingw/releases).
rem Put llvm-mingw\bin on PATH first. MSVC cannot build this file (it uses GNU inline assembly).
cd /d "%~dp0"
i686-w64-mingw32-clang++ -std=c++20 -O2 -Wall -Wextra -shared -static -Iinclude TGCConvertPop.cpp -o TGCConvertPop.dll -Wl,--kill-at || exit /b 1
echo built plugin\TGCConvertPop.dll
