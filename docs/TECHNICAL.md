# Technical notes (v2game.exe 3.04 Steam)

All addresses are VAs of the 3.04 listing; the plugin rebases them at runtime. Each patch site is compared byte for byte first, and a feature is skipped (with a log line) when it differs.

## Console table

- `FUN_00423990` pushes 60 static 0x4C-byte entries (`0x00F09740`..`0x00F0A910`). The plugin copies them into its own table, appends its commands and retargets the loop: `push 0x3C` @ `0x4239C2`, `mov ebx,table` @ `0x4239D0`, `mov eax,0x3B` @ `0x4239D5`, `cmp ebx,end` @ `0x423A3B`.
- Entry layout:
  - `+0` enabled
  - `+4` name
  - `+8` alias count
  - `+0xC` aliases
  - `+0x18` description
  - `+0x1C` handler `Result* __cdecl(Result*, vector<Token>*)`
  - `+0x20` argc
  - `+0x24` argument names

## Game data

| Object | Fields |
|---|---|
| Pop | `+0xC` id, `+0x58` size, `+0x5C` previous size, `+0x64` province, `+0x6C` culture, `+0x70` religion, `+0x27C` next; vtable `+0x20` = alive |
| Province | `+0x58` id, `+0x12C` owner, `+0x194` per-pop-type lists (stride 0x10) |
| Country | `+0xE54` accepted cultures (list), `+0xE64` primary culture, `+0xE68` religion |

## Game routines used

| VA | Routine |
|---|---|
| `0x4B8E80` | `Pop_MoveToProvince(Province*, Pop*, bool)` stdcall (used by `move_pop`) |
| `0x955FD0` | `Pop_SetSize`: ESI = pop, EAX = size; clamps to 2,000,000 and fixes employment on shrink (used by `reduce_pop`) |
| `0x4B61A0` / `0x4B6290` | add / remove core: stdcall `(Province*, CTag* {tag, index}, bool history)`, then country `+0x1434` = 1 (as the script effects do); cores = CTag vector at province `+0x148` |
| `0x522310` | `AddAcceptedCulture` (ESI = country) |
| `0x5F2AE0` | list remove (ESI = list) |
| `0x4A2F70` / `0x5677B0` | culture / religion lookup (EAX = map) |
| `0x55C6C0` | country by tag |
| `0x409350` | `std::string::assign` |

## Pop merge bug

At `0x4BCB2E` the merge check is `mov ecx,[edx+0x6C]; cmp ecx,ecx; je found`. It compares the culture with itself, so any pop of the same type and religion matches. A code cave replaces it with a real comparison: always during `migrate_pop`/`migrate_all_pops`, and game-wide when `fix_merge_everywhere=1`.

## Top bar tab

Topbar class vtable `0x00E113D0`:

| Slot | Function |
|---|---|
| [0] | build `0x7129A0` (also called directly from the constructor at `0x70D090`) |
| [1] | deleting dtor `0x70D0C0` |
| [2] | show `0x70D300` |
| [3] | hide `0x7B3660` |

Topbar members: `+0x20` the `topbar.gui` window, `+0x24` the owner, which provides the GUI layer (vt `+0x3C`).

GUI factory `[0x01258A80]`:

- vt `+0x2C` = `create(const std::string& type, layer)`.
- `+0x34` = gui type map (vt `+4` find, returns 0 if missing, checked before creating).

Window:

- vt `+0x34` find child by name
- vt `+0x50` destroy
- `(window+0x18)` vt `+0x34`/`+0x38` show/hide
- byte `+0x67` visible

Button clicks: the observable is at `button+0x54` (vt `+4` AddObserver). The observer is a copy of the topbar's own `CButtonObserverGlue` at `topbar+0x218`, with the click method (`+8`) pointing to a register-preserving thunk.

The plugin destroys its windows when the topbar rebuilds or is destroyed, mirroring what the topbar does with its own window, so no pointer outlives its window.

## Blocs

- War participants: `War+0x2C` attackers and `+0x3C` defenders, both vectors of CTag `{char tag[4], int index}`.
- `War_AddAttacker 0x934280` / `War_AddDefender 0x935390`: `__stdcall (War*, bool join, bool b, CTag)`. A declaration (`0x913510`) calls them for the leaders and then for their allies with `join=1`, call-ally accepts do the same, and loading a save and crisis/rebel wars use `join=0`.
- The plugin detours both (9-byte prologue moved to a trampoline). After a `join=1` add, the other members of the country's bloc are added to the same side. The attacker of a fresh declaration is held back until the defender exists.
- A country is never added to both sides.
- Country flags: CFlags at `country+0x1B0`.
  - vt `+8` = find(const char*) returns an entry (name GStr `+0`, set byte `+0x1C`).
  - `0x43C450` adds a flag (ESI = flags, EAX = name).
  - The flags sit in a ternary trie: root at `flags+4`, node `{entry, char, lo, hi, eq}`.
- Country tags: 64 hash buckets of CTag vectors at `countries+0x14` (16 bytes each).

## Bloc map

- `0x7FCA30` builds the political palette: `__stdcall (colourMap)` with colourMap = `mapView->vt38()+0x20BC`.
  - colourMap `+0x1C` is a vector of entry pointers, one per province, from index 1.
  - Entry `+0` is the owner colour and `+4` the controller colour, both ARGB. Entry `+0xC` is the province.
- The calls at `0x66C4C3` (political map mode, ESI = map view) and `0x422A32` are redirected to the plugin. When the bloc map is on, the plugin runs the original and then overwrites the colours.
- Redraw: map view `[+0x24]` holds 22 per-mode dirty flags; flag 1 makes the game rebuild the political palette on its next map update (`0x654530`).

## Power blocs

- Flags: `bloc_<name>` (membership), `blocleader`, `blocid_<identity>` (on the leader).
- Relations: `country+0xBE8[other]+0x3C` (×1000, ±200). Prestige: `country+0xEA0` (×1000).
- Monthly effects: the call to `DayTick` at `0x683AB1` (stdcall world) is redirected. When the month changes (date `world+0xB0C`, month table `0xF1027C`), the plugin applies the identity effects.
- Text boxes: window vt `+0x3C` finds a text box. The plugin assigns its `std::string` at `+0xDC` and calls `SetText 0xA5ECC0` (ESI = `[box+0xA0]`), as the game does everywhere.

## Invite button

- Province/country view (shown when you click the map): `world+0xB24` → vt `+0x124`.
  - Its `+0x20` is the window (`+0x67` = visible).
  - Its `+0x24` points to the selected country's CTag at `+0x128`/`+0x12C`, the same fields the diplomatic map mode reads (`0x7FCD09`).
- Visibility of the `v2bloc_diplo` window is synced from `CInGameIdler_Update` (vtable slot `0xE0A350`, thiscall 1 arg), every 6th frame.
- `yesmen` cheat: the byte `0x12589A7`.

## Treaties

- Province transfer calls `CSecedeProvinceEffect::Execute` (`0x887420`, thiscall `(effect, scope, unused)`) with a fake effect and scope, exactly as an event's `secede_province` would:
  - fake effect: `+0x20`/`+0x24` = new owner CTag;
  - fake scope: `+0x08`/`+0x0C` = current owner CTag, `+0x10` = province id.
- Capital: `country+0xBE4`.
- Selected province: the province view's `+0x24`, a `Province*` (owner CTag at `+0x128`/`+0x12C`, id at `+0x58`).
- A population exchange runs `migrate_all_pops <AI> <you> accepted` and `migrate_all_pops <you> <AI> accepted`, so the strict culture merge check applies.
