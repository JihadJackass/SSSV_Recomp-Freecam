# Free Camera (Middle Mouse Orbit) for Space Station Silicon Valley: Recompiled

A gameplay mod for [Space Station Silicon Valley: Recompiled](https://github.com/Cellenseres/SSSV_Recomp)
that adds smooth, mouse-driven camera control: hold the **Middle Mouse Button**
and move the mouse to freely orbit the camera around your current animal,
instead of being limited to the vanilla 45-degree C-button snaps.

<p align="center">
  <img width="700" alt="In-game screenshot: the camera orbited to a custom angle around the player's animal using the middle mouse button" src="https://github.com/user-attachments/assets/b69f4a1a-316e-4ea1-8aa6-fc3130fb5cdd" />
  <br>
<<<<<<< Updated upstream
  <em>Orbit the camera to any angle by holding the Middle Mouse Button — here at an angle the vanilla 45-degree snaps can't reach.</em>
=======
  <em>Orbit the camera to any angle by holding the Middle Mouse Button, including angles the vanilla 45-degree snaps can't reach.</em>
>>>>>>> Stashed changes
</p>

<p align="center">
  <img width="532" alt="The mod's Configure menu showing sliders for recenter delay and orbit sensitivity, plus toggles for never-recenter and invert" src="https://github.com/user-attachments/assets/61bdc3e1-d2f2-4307-a6f7-056324ce50e3" />
  <br>
  <em>Everything is adjustable live from the in-game Mods → Configure menu: recenter delay, never-recenter, sensitivity, and inversion.</em>
</p>

## Features

- Smooth, continuous 360-degree yaw orbit while holding MMB, with 1:1 mouse response
- Configurable in-game with no rebuilds: recenter delay, "never recenter" mode,
  orbit sensitivity, and horizontal inversion, all in **Mods → Configure**
- Plays nice with vanilla controls: C-Left / C-Right snapping and
  C-Up / C-Down zoom still work exactly as before
- Respectful of the game's design: only active in the player-controlled
  chase-camera modes. Cutscenes, waypoint cameras, fixed rooms, and tank
  mode are untouched, and the game's own camera smoothing and wall avoidance
  remain fully in effect
- Uses the game's native auto-recenter suppression timer (the same one the
  C-buttons arm), so the delayed return-to-heading behaves exactly like a
  vanilla camera rotation would

## Requirements

- [Space Station Silicon Valley: Recompiled](https://github.com/Cellenseres/SSSV_Recomp)
  v0.1.2 or later, set up with your own legally obtained US 1.0 ROM
- Windows or Linux (prebuilt); macOS works but you must build the small
  native companion library yourself (one command, see below)

This mod contains no game assets or game code.

## Installation

Grab the latest release (or use the files in `dist/`). The mod is **two
files** that must sit **next to each other** in your mods folder:

| File | What it is |
|---|---|
| `sssv_freecam_mmb_orbit.nrm` | The mod |
| `sssv_freecam_native.dll` / `.so` / `.dylib` | Mouse-reading companion library for your platform |

Mods folder locations:

- **Windows:** `%LOCALAPPDATA%\SSSVRecompiled\mods`
- **Linux:** `~/.config/SSSVRecompiled/mods`
- **Portable mode** (`portable.txt` next to the exe): `mods` folder next to the exe

Copy both files in, launch the game, and enable the mod in the **Mods** menu.

## Configuration

Open **Mods → Free Camera (Middle Mouse Orbit) → Configure** in-game:

| Option | Range | Default | What it does |
|---|---|---|---|
| Recenter delay (seconds) | 0-30 | 3 | How long after releasing MMB before the camera starts drifting back behind your animal. 0 is roughly vanilla behavior. |
| Never recenter | Off / On | Off | The camera always stays where you put it (overrides the slider). Wall avoidance still works. |
| Orbit sensitivity | 0.05-1.0 | 0.22 | Camera rotation per mouse movement. |
| Invert horizontal | Off / On | Off | Flip the orbit direction. |

All settings apply live.

> **Note:** the recenter *drift* itself is the game's own (fairly gentle)
> realignment logic. The slider controls when it starts, not how fast it moves.

### Why is there no vertical (pitch) control?

Deliberate: the game rewrites the camera's pitch target every frame from
internal per-distance tables, so mouse pitch would be overwritten instantly.
For vertical framing, use the vanilla zoom steps (C-Up / C-Down). The port's
own Input menu lets you bind those to the mouse wheel, which pairs nicely
with this mod.

## Building from source

### The mod (`.nrm`)

Requirements: clang with MIPS support (**LLVM 18.x**; the 19.1.0 release
binaries have broken MIPS support), `ld.lld`, and
[RecompModTool](https://github.com/N64Recomp/N64Recomp/releases/tag/mod-tool-release).

```sh
make CC=clang-18 LD=ld.lld-18
./RecompModTool mod.toml build
# -> build/sssv_freecam_mmb_orbit.nrm
```

The `syms/` folder contains the two symbol files from
[SSSVRecompSyms](https://github.com/Cellenseres/SSSVRecompSyms). If the port
updates its symbols, replace them and rebuild.

### The native companion library

Single C file, zero build dependencies (SDL2 is resolved at runtime from the
running game process):

```sh
# Linux
gcc -shared -fPIC -O2 -o sssv_freecam_native.so native/sssv_freecam_native.c

# macOS
clang -dynamiclib -O2 -o sssv_freecam_native.dylib native/sssv_freecam_native.c

# Windows (MSVC developer prompt)
cl /LD /O2 native\sssv_freecam_native.c /Fe:sssv_freecam_native.dll

# Windows cross-compile from Linux
x86_64-w64-mingw32-gcc -shared -O2 -o sssv_freecam_native.dll native/sssv_freecam_native.c -static-libgcc
```

## How it works

SSSV stores camera yaw as a float where 256 = a full circle: a target yaw the
C-buttons snap in 32-unit steps, a smoothed current yaw the engine chases,
and a suppression timer that delays the auto-recenter after manual rotation.

This mod places a `RECOMP_HOOK` on the game's per-frame camera input
dispatcher. While MMB is held (and the active camera is player-controlled, in
a chase mode), mouse X deltas feed the target yaw, the smoothed yaw is
snapped to it for 1:1 response, and the game's own suppression timer is armed
to the configured delay. Everything downstream (smoothing, wall avoidance,
the eventual return-to-heading) is stock game logic.

Recompiled MIPS code can't call SDL, so mouse state comes from the tiny
native companion library declared in the mod manifest. It resolves
`SDL_GetGlobalMouseState` out of the game's own process, diffs cursor
positions into deltas, and reports button state and window focus (input is
ignored while the game is unfocused).

All symbols and struct offsets were verified against the
[mkst/sssv](https://github.com/mkst/sssv) decompilation and the port's
shipped symbol files.

## Known limitations

- Yaw only (see above)
- Camera collision is whatever the game already does; at unusual angles the
  camera can clip level geometry, same as vanilla allows
- Targets the US 1.0 layout, which is the only ROM the port accepts

## Credits

- [mkst](https://github.com/mkst/sssv) and the SSSV decompilation
  contributors, whose camera reverse engineering made this mod's precision
  possible
- [Cellenseres](https://github.com/Cellenseres/SSSV_Recomp) for the SSSV
  Recompiled port and its symbol files
- [Mr-Wiseguy and the N64Recomp project](https://github.com/N64Recomp/N64Recomp)
  for the recompilation and modding toolchain
- The [Zelda64Recomp mod template](https://github.com/Zelda64Recomp/MMRecompModTemplate),
  which this mod's build setup is adapted from

## Legal

This mod contains no game assets and no code from the original game. You must
own Space Station Silicon Valley and provide your own copy to the port. This
project is not affiliated with the SSSV Recompiled project, the decompilation
project, or the original game's rights holders.
