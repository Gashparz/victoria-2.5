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
