# Victoria II – Console Commands, Top Bar Tab & Save Editor

New developer-console commands for **Victoria II 3.04 (Steam)** that move, convert and resize pops and edit accepted cultures, a **COMENZI** tab in the top bar that lists them in game, and a browser-based **save editor** with an interactive province map.

Everything is mod-agnostic: it works with vanilla, **GFM**, **TGC** or any other mod.

> Made for single player. Every player in multiplayer would need the same files, and the commands change the game state on one machine only.

## Features

### Console commands

Open the console with `~`. Province ids come from the built-in `showprovinceid` command.

| Command | What it does |
|---|---|
| `convert_pop <prov> [culture\|-\|owner] [religion\|-\|owner] [filters]` | Converts culture and/or religion. With no arguments it uses the owner's primary culture and state religion |
| `list_prov_cultures <prov>` | Lists every pop with its id, type, culture and size, plus totals |
| `list_prov_religions <prov>` | Same, by religion |
| `migrate_pop <from> <to> <filters>` | Moves the pops that match **all** filters to another province |
| `migrate_all_pops <TO> <FROM> [filters] [accepted]` | Moves every pop of TO's primary culture (with `accepted`, also its accepted cultures) from country FROM into TO's provinces, spread evenly |
| `accept_culture <TAG> [cultures]` | Adds accepted cultures. With only the tag, it lists them |
| `unaccept_culture <TAG> <cultures>` | Removes accepted cultures |
| `double_pop <prov> [filters]` | Doubles pop sizes |
| `scale_pop <prov> <percent> [filters]` | Scales pop sizes: 200 doubles, 50 halves |

A **filter** can be a culture (`polish`), a religion (`catholic`), a pop type (`farmers`) or a pop id taken from the list commands.

Examples:

```
convert_pop 652 serb - bulgarian farmers
migrate_pop 123 555 polish catholic
migrate_all_pops POL GER accepted
double_pop 652 serb
```

`migrate_pop` merges a pop only into a destination pop with the same type, culture **and** religion. The vanilla merge ignores culture, which is why moved Serbs used to become Bulgarians. Set `fix_merge_everywhere=1` in `mod/TGCConvertPop.ini` to apply the same fix to the whole game.

### Top bar tab

A **COMENZI** tab appears to the right of *Military* and opens a window listing the commands; `ESC` or `X` closes it. It is defined in `interface/zz_v2commands.gui` (edit `position` there to move it). Set `topbar_tab=0` in the ini to turn it off.

### Save editor

`save-editor/` is a single HTML file that runs offline in Chrome or Edge. It can:

- Edit owners, controllers, cores and accepted cultures.
- Edit pops: culture, religion, type, size, delete and clone.
- Run bulk conversions.
- Show an interactive map (political, controller, culture, religion and population modes).

It reads cultures, religions, names, colors and the map from your game folder plus the mods you tick, layered the way the launcher does it (`replace_path` and dependencies included). Saving makes byte-exact edits, so it never rewrites the whole save. The interface is in Romanian.

## Requirements

- Victoria II **3.04**, Steam `v2game.exe` (SHA-256 `62d48c204364dd706584777c2e2b3c7ab3c5f1dd0170872554943575d53d6648`). The plugin checks the bytes at every patch site and turns a feature off, with a line in the log, if they don't match.
- **[The Grand Loader](https://github.com/The-Grand-Combination/The-Grand-Loader)** (`dinput8.dll`), which loads the plugin.
- Optional: **[Victoria 2 Unofficial Patch 3.05 / V2DLL](https://github.com/artyom-kuznetsov/V2DLL_Victoria-2-Extension)**. The plugin was tested alongside it.

## Install

See **[docs/INSTALL.md](docs/INSTALL.md)**. In short:

1. Install The Grand Loader into the game folder.
2. Copy `mod/` and `interface/` from the release zip into the game folder.
3. Start the game normally (Steam → Play → launcher).

## Build from source

The plugin needs **llvm-mingw**. MSVC cannot build it, because it uses GNU inline assembly.

```
plugin/build.cmd        (Windows)
plugin/build.sh         (Linux/macOS)
```

The save editor needs Python 3:

```
python save-editor/build.py      -> save-editor/Victoria2 Save Editor.html
```

`build.py --pack file.js` can embed an offline data pack (`const BUILTIN_PACK = {...}`). No pack is shipped, because mod data belongs to the mod authors.

`./package.sh` builds both and writes the release zip to `dist/`.

## How it works

See **[docs/TECHNICAL.md](docs/TECHNICAL.md)** for addresses, structures and hooks. The plugin:

- Extends the engine's static console table.
- Calls the game's own routines for moving pops, setting pop size and adding accepted cultures.
- Builds the top bar tab with the engine's GUI factory, using the same observer glue the game uses for its own buttons.

## Credits

- [maxioten/Victoria2-Reverse-Engineering](https://github.com/maxioten/Victoria2-Reverse-Engineering): structure and function research.
- [artyom-kuznetsov/V2DLL_Victoria-2-Extension](https://github.com/artyom-kuznetsov/V2DLL_Victoria-2-Extension): GUI hooking techniques (top bar build, button observer glue).
- [The-Grand-Combination/The-Grand-Loader](https://github.com/The-Grand-Combination/The-Grand-Loader): the plugin loader.

Victoria II is a trademark of Paradox Interactive. This project is not affiliated with or endorsed by Paradox Interactive.

## License

[MIT](LICENSE)
