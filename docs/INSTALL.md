# Installation

The game folder is usually `C:\Program Files (x86)\Steam\steamapps\common\Victoria 2`. Close the game before copying files.

## 1. The Grand Loader

Download it from <https://github.com/The-Grand-Combination/The-Grand-Loader> and follow its instructions: `dinput8.dll` goes next to `v2game.exe`. It loads every plugin declared by a `mod\<name>.ini`.

## 2. This project

From the release zip, copy into the game folder:

```
mod\TGCConvertPop.ini
mod\TGCConvertPop.mod
mod\TGCConvertPop\TGCConvertPop.dll   (+ README.txt)
interface\zz_v2commands.gui
interface\zz_v2commands.gfx
```

The `interface` files only add new names, so they never replace a game or mod file.

## 3. Optional: Unofficial Patch 3.05 (V2DLL)

V2DLL installs as `lua51.dll`, and the original `lua51.dll` is renamed to `lua51_real.dll`. Both loaders work together. Two settings in `v2dll_settings.ini` matter:

- `PLAYER_BUTTONS=0`: V2DLL hooks the same top bar function as the COMENZI tab. If it is 1, the tab is skipped (the log says so) and everything else still works.
- `ENABLE_VERSION_LABEL`: both The Grand Loader and V2DLL patch the main menu version label. If you see problems there, set it to 0.

## 4. Play

Start the game the usual way: Steam → Play → launcher → tick your mod (GFM, TGC…) → Play. Ticking "Console Commands (any mod)" is not required while `require_mod=0` in the ini.

Open the console with `~` and type `list_prov_cultures 1`.

## Settings (`mod\TGCConvertPop.ini`)

| Key | Default | Meaning |
|---|---|---|
| `require_mod` | 0 | 1 = the commands work only when the mod is ticked in the launcher |
| `fix_merge_everywhere` | 0 | 1 = moving pops merge only with the same culture, game-wide |
| `topbar_tab` | 1 | 0 = no COMENZI tab |

## Troubleshooting

Look at `the_grand_loader.log` next to `v2game.exe`. Every hook writes one line there, and every refused command is logged too.

## Uninstall

Delete `mod\TGCConvertPop*` and `interface\zz_v2commands.*`.
