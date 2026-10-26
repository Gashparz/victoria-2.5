# Victoria 2 launcher that remembers your mods

Drop-in replacement for `victoria2.exe` (the program Steam starts). Rename the original to
`victoria2_original.exe`, put this `victoria2.exe` in its place.

- Tick mods, reorder them (Move up / Move down); the choice is saved in `launcher\mods_selected.txt`.
- "Next time start directly" skips the window; hold **Shift** while starting to see it again.
- "Original launcher" runs `victoria2_original.exe`.
- Works with any mod set (or none). Steam "Verify integrity" restores the original exe.

Build: `i686-w64-mingw32-windres res.rc -O coff -o res.o && i686-w64-mingw32-clang++ -O2 -municode -mwindows -static launcher.cpp res.o -o victoria2.exe -lcomctl32 -lole32 -loleaut32 -luuid -lshell32`
(`logo.ico` comes from the game's `launcher` folder.)
