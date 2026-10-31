# Victoria 2.5

**Power blocs, Victoria 3–style treaties, historical diplomacy and a modern toolset for Victoria II, without replacing a single game file.**

Victoria 2.5 is a plugin and tool collection for **Victoria II: Heart of Darkness 3.04 (Steam)**. It adds systems from later Paradox games (power blocs and negotiated treaties from Victoria 3, treaty screens inspired by HOI4 and Europa Universalis) and a set of quality-of-life tools, while keeping the game itself untouched.

It is **mod-free**: it works with vanilla and with any mod. It is also **extra compatible with TGC** (The Grand Combination) through an optional submod.

> Single player. In multiplayer every player would need the same files and settings, and several features change the game state.

---

## Contents

- [What it adds](#what-it-adds)
- [Installation](#installation)
- [Quick start](#quick-start)
- [Configuration](#configuration)
- [Historical treaties as data](#historical-treaties-as-data)
- [How it works](#how-it-works)
- [Building from source](#building-from-source)
- [Repository layout](#repository-layout)
- [Credits and license](#credits-and-license)

---

## What it adds

### 1. Power blocs (Victoria 3 style)

Mega-alliances that sit above the game's normal alliances. When a member goes to war (it declares war, is attacked, or joins as an ally), every other living member joins the same side. Members never leave on their own.

- **Leader, identity and cohesion**: four identities (Military, Trade, Empire, Religious), each with its own monthly effects. Cohesion depends on relations, wars between members and the leader's weight; below 20 the bloc is *Fractured* and only the leader's wars pull it in.
- **Bloc panel**: members, population, prestige, provinces, relations, cohesion bar and identity effects, one bloc per page. It has buttons to create or leave a bloc, change the identity, disband the bloc and toggle the map mode.
- **Bloc map mode**: the political map colours each bloc; countries outside blocs turn grey.
- **From diplomacy**: select a country in the Diplomacy screen and a bar under it offers *Form a bloc* / *Invite to bloc*. The AI decides from relations, religion, size, prestige and cohesion; with the `yesmen` cheat it always accepts.

### 2. Treaties (Victoria 3 / HOI4 / EU style)

A negotiation window opened from Diplomacy (*Propose treaty*):

- **Articles on both sides**: cede provinces, pay or demand money, send their people home, bring your people home, pay for safe transport. Each article shows its own acceptance score, and each can be removed with one click.
- **Acceptance score with Victoria 3 thresholds**: at **+30** or more they sign, at **0** or below they refuse, and in between there is a chance. The window shows a bar and the reasons (relations, bloc, relative power, religion, homeland, their own people...).
- **Counter-offers**: when they would refuse, the window shows how much money would make them sign. *Accept their terms* adds it to your side.
- **Map view**: while the window is open, the political map shows what you give (red) and what you get (green).
- **Pick on map**: click provinces to add or remove them. One click can also add every province where your (or their) people are the majority.
- **Treaty names** after the partner's capital, e.g. *Treaty of Prague*.

### 3. Population exchanges that respect nations

Population articles move people "home" by rules that follow the game's own national logic:

- A country's **primary culture never leaves it**.
- A culture only one side counts as its own goes to that side.
- A culture both sides count as their own goes to the side whose **culture group** it belongs to. For example, Bohemian Germans go to Germany, while Bavarians stay in Germany in a swap with Austria, because both countries are Germanic. Culture groups are read from the active mod's `common/cultures.txt`.
- No treaty may move more than **30 %** of a country's people.
- **Migration losses**: about 10 % of the people who move die on the way, **unless you pay for safe transport**, which adds +10 acceptance. The AI pays when it can afford it.
- Moving pops never hits the game's 2,000,000-per-pop cap, so the numbers shown are the numbers that move.

### 4. AI diplomacy and history

- **Historical treaties**, about 80–85 % of what happens: Aigun/Peking, Gadsden, Turin (Savoy and Nice), the Ionian Islands, Alaska, the Gold Coast sales, Saint Petersburg (Sakhalin ↔ Kurils), Cyprus, Thessaly, Heligoland–Zanzibar, the 1898 leases, the Carolines, Simla, the Danish West Indies, the Schleswig and Saar plebiscites, the population exchanges of 1913, 1919 and 1923, and more. A treaty fires only when the world in your game allows it: both countries exist, the giver still owns the land, and they are not at war.
- **Same odds as the original game**: treaties that exist as decisions or events in the loaded files (vanilla or TGC) are left to the game, so they keep identical conditions, mean time to happen and AI choices. The plugin recognises them when they happen and records them.
- **Plausible AI deals** for the rest: border swaps, cessions to a much stronger neighbour with a core and a majority, arrangements inside a bloc, and population exchanges after conquests. Both sides judge them by the same rules as the player's treaty window.
- **AI requests to you**: a *Proposal from …* window with what you give, what you get, why, and what refusing costs (Accept / Decline; it expires after 30 days). A country that was refused remembers it.
- **News**: a short notice at the top of the screen and a *Recent treaties* list.

### 5. Console commands

Open the console with `~`. Province ids come from `showprovinceid`.

| Command | What it does |
|---|---|
| `convert_pop <prov> [culture\|-\|owner] [religion\|-\|owner] [filters]` | Change culture and/or religion of pops |
| `list_prov_cultures <prov>` / `list_prov_religions <prov>` | List pops with id, type, culture/religion and size |
| `migrate_pop <from> <to> <filters>` | Move matching pops to another province |
| `migrate_all_pops <FROM> <TO> [accepted] [filters]` | Move TO's people living in FROM into TO, spread evenly |
| `double_pop`, `scale_pop <prov> <percent>`, `scale_country_pop <TAG> <percent>` | Resize pops |
| `accept_culture`, `unaccept_culture` | Edit accepted cultures |
| `add_core`, `remove_core` | Edit cores |
| `bloc_add`, `bloc_remove`, `bloc_list`, `bloc_invite`, `bloc_identity`, `bloc_leader`, `bloc_info`, `bloc_disband`, `bloc_map` | Power blocs |
| `treaty`, `treaty_propose`, `treaty_population` | Treaties from the console |
| `treaty_history [request]` | Status of every historical treaty; `request` makes an AI country ask you now |

A filter is a culture, a religion, a pop type or a pop id. The vanilla bug where moving pops merge into any pop of the same type and religion, whatever the culture, is fixed for these commands, and optionally for the whole game.

### 6. Interface

- A **top bar tab** next to Military opens the Victoria 2.5 overview: Power Blocs, Treaties, recent treaties and the command reference.
- **Native look**: parchment windows built from the game's own art, black Victoria fonts, grey header bars, and tooltips on every button.
- **Game names everywhere**: the plugin shows countries and provinces by the names the game itself displays, never by tags or ids.

### 7. A launcher that remembers your mods

A drop-in replacement for `victoria2.exe` (the program Steam starts):

- tick your mods and order them;
- the choice is saved;
- optionally the game starts directly, with **Shift** at start showing the window again;
- the original launcher is kept as `victoria2_original.exe`.

### 8. Save editor

`SaveEditor/Victoria2 Save Editor.html` is a self-contained browser tool for `.v2` saves. It has an interactive province map, pop editing and mod layering.

### 9. Diagnostics

Every feature logs to `the_grand_loader.log`, with monthly performance counters, so problems can be traced without a debugger.

---

## Installation

Victoria 2.5 runs as a plugin of **The Grand Loader** (`dinput8.dll`), together with the community **V2DLL** (Victoria 2 Unofficial Patch 3.05 0.6.2.4).

1. Copy the release package into the game folder (`Steam\steamapps\common\Victoria 2`). It contains:
   - `mod\TGCConvertPop\` with the plugin, its treaty data and the README;
   - `mod\TGCConvertPop.ini` with the settings;
   - `interface\zz_v2commands.gui`, `interface\zz_v2commands.gfx` and `localisation\zz_v2commands.csv`;
   - optionally `mod\TGCTreaties\` and `mod\TGCTreaties.mod`, for TGC players;
   - optionally the launcher.
2. Create the window textures once from your own game files. They are game art, so they are not shipped:
   ```
   python tools/make_textures.py "<Victoria 2>/gfx/interface" "<Victoria 2>/gfx/interface"
   ```
3. Start the game. The log shows `Victoria 2.5 build …`.

**With TGC:** in the launcher, tick *TGC* and then *Victoria 2.5 – TGC Treaties*. Without TGC, do not tick the TGC submod; the base treaties load on their own.

Internal file names (`TGCConvertPop.dll`, `mod\TGCConvertPop\`) are kept from the project's first version so existing installs and saves keep working.

---

## Quick start

| I want to… | Do this |
|---|---|
| Found a bloc and invite a country | Diplomacy → select the country → **Form a bloc** under the window |
| Exchange provinces or people | Diplomacy → select the country → **Propose treaty** → add articles → **Propose treaty** |
| See the blocs on the map | Top bar tab → Power Blocs → **Bloc map** (political map mode) |
| See which historical treaties can still happen | Console: `treaty_history` |
| See an AI request right now | Console: `treaty_history request` |

---

## Configuration

`mod\TGCConvertPop.ini`, section `[convert_pop]`:

| Key | Default | Meaning |
|---|---|---|
| `require_mod` | `0` | 1 = the plugin works only when its mod is ticked in the launcher |
| `fix_merge_everywhere` | `0` | 1 = also fix the pop merge bug for the game itself |
| `topbar_tab` | `1` | the top bar tab and every window |
| `blocs` | `1` | power blocs (war joining, monthly effects, map mode) |
| `ai_treaties` | `1` | historical and AI treaties, requests to the player |
| `treaty_ai_years` | `10` | about one generated AI–AI deal every N years worldwide |
| `treaty_request_years` | `6` | an AI country asks you at most about once every N years |
| `migration_loss` | `10` | percent of the people who move that die without paid transport |
| `transport_cost` | `50` | price of safe transport per 1,000 people |

---

## Historical treaties as data

Treaties are plain text files, so anyone can add or change them:

- `mod\TGCConvertPop\treaties\base.txt` is always loaded and works with any mod.
- `<mod>\treaties\*.txt` is loaded for every ticked mod. The TGC submod uses this to add treaties converted from TGC's own events, with TGC's tags, provinces and state regions.
- A later file replaces an entry with the same `id`.

```
treaty = { id = alaska years = { 1866 1880 } kind = cession from = { RUS } to = { USA }
  provinces = { 1 2 3 4 5 2599 } chance = 50 accept = 80
  native = { "decisions/ACW.txt:alaskan_purchase" "events/RUSFlavor.txt:32509" }
  source = "vanilla decision alaskan_purchase -> event 32509 (A 80 / B 20)"
  title = "Alaska Purchase" why = "Russia sells Alaska to the United States for 7.2 million dollars." }
```

`kind` is `cession`, `swap` or `population`. Provinces can be given as ids, as `regions` (from the active map's `region.txt`) or as `cores_of`. Timing is `chance` (percent per month) or `mtth` (months, as in the game). `accept` is the AI's answer, taken from the original event's `ai_chance`. When `native` names a decision or event that exists in the loaded files, the game runs the treaty itself. The full format is described at the top of `base.txt`.

---

## How it works

Victoria 2.5 changes no game file. The plugin is a 32-bit DLL loaded by The Grand Loader that hooks `v2game.exe` 3.04 at runtime. Every patch site is checked byte for byte first; if anything differs, that feature is switched off and the log says so.

| Area | Hook |
|---|---|
| Console | the command table is copied and extended |
| Top bar tab and windows | topbar vtable (build, show, hide, destructor); windows are created through the game's own GUI factory from `zz_v2commands.gui` |
| Buttons | button observers built from a live game observer |
| Frame updates | in-game idler vtable slot, checked about 10 times a second |
| Monthly logic | the day-tick call (bloc effects, AI treaties, performance report) |
| Blocs in wars | `War_AddAttacker` / `War_AddDefender` detours |
| Map modes | the political palette builder, recoloured for blocs and treaties |
| Diplomacy bar | the diplomacy window constructor, which captures the window; the selected country is read from the window itself |
| Native treaties | `CSecedeProvinceEffect::Execute` vtable slot, to recognise historical treaties carried out by game events |
| Names | the game's own country and province name routines |
| Money | the treasury, as the treasury effect uses it |

The addresses and structures are documented in [`docs/TECHNICAL.md`](docs/TECHNICAL.md).

---

## Building from source

The plugin and the launcher are cross-compiled with **llvm-mingw** (i686):

```
cd plugin
i686-w64-mingw32-clang++ -std=c++20 -O2 -shared -static -Iinclude -I. TGCConvertPop.cpp -o TGCConvertPop.dll -Wl,--kill-at

cd launcher
i686-w64-mingw32-windres res.rc -O coff -o res.o
i686-w64-mingw32-clang++ -O2 -municode -mwindows -static launcher.cpp res.o -o victoria2.exe -lcomctl32 -lole32 -loleaut32 -luuid -lshell32
```

- The interface files come from `tools/gen_gui.py`; the textures come from `tools/make_textures.py`.
- `package.sh` builds the release package.
- `save-editor/build.py` builds the save editor.

---

## Repository layout

```
plugin/        the DLL: TGCConvertPop.cpp, ai_treaties.inc (history + AI), treaty_v3.inc (treaty window)
interface/     zz_v2commands.gui / .gfx (generated)
localisation/  tooltip texts
mod/           .mod files, ini, base treaties, the TGC Treaties submod
launcher/      the mod-remembering launcher
save-editor/   the browser save editor
tools/         GUI generator, texture builder
docs/          installation and technical notes
```

---

## Credits and license

- **The Grand Loader** and **V2DLL / Victoria 2 Unofficial Patch** by their authors. Victoria 2.5 is a plugin for them.
- **TGC – The Grand Combination**: the TGC submod converts its treaty events into this system and needs TGC itself.
- Victoria II is © Paradox Interactive. No game files or game art are included.

Released under the [MIT License](LICENSE).
