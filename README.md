# rondo-nx

A native port of Castlevania: Rondo of Blood (PC Engine Super CD-ROM², 1993), built from your own game files. PC build first, Nintendo Switch next.

No game data is in this repository. The game's code is translated locally from coverage traces of your own disc, and the app reads your disc image and System Card at run time.

## Status

**Version 1.2.1 is the current Switch release** (fixes on top of 1.2's experimental widescreen). It has been played on a V1 Switch
(Atmosphère) and runs well, but it has not been played to the end on the Switch yet. Every stage, alternate stage and
boss was traced, and the translated code matches the emulator frame by frame. Release notes:
[docs/release/rondo-nx-1.2.1.md](docs/release/rondo-nx-1.2.1.md) (1.2: [docs/release/rondo-nx-1.2.md](docs/release/rondo-nx-1.2.md); 1.1: [docs/release/rondo-nx-1.1.md](docs/release/rondo-nx-1.1.md); 1.0: [docs/release/rondo-nx-0.5.0.md](docs/release/rondo-nx-0.5.0.md), published as 1.0).

## Download and install (Switch)

You need a Switch with custom firmware (Atmosphère), your own System Card 3.0 BIOS, and your own disc with the
English fan translation v1.03 applied (a `.cue` with its `.bin` tracks). Nothing from the game is included.

1. Download `rondo-nx-v1.2.1-switch.zip` from this repository's **Releases** page and extract it to the root of your
   SD card. It makes `switch/rondo-nx/`.
2. Copy the System Card to `switch/rondo-nx/syscard3.pce`.
3. Copy the English `.cue` and all of its `.bin` tracks into `switch/rondo-nx/game/`. Optional: the Japanese original
   in `switch/rondo-nx/game-jp/` adds a Japanese option.
4. Hold **R** while opening any game to get the homebrew menu with full memory, then start **rondo-nx**.

To report a problem, open an issue with where in the game it happened, what you saw, your Atmosphère version and the
language you were playing in.

## How it works

1. **Coverage tracing** (`tools/tracer`): a patched Geargrafx build logs every piece of code the game runs, including code it loads from disc mid-game.
2. **Translation** (`tools/xlat/gen.py`): turns the traces into C++ blocks. Each instruction runs with its operands baked in, and blocks are matched to the right code overlay by their bytes. Anything not traced falls back to the interpreter.
3. **Runtime** (`runtime/`, `app/`): a standalone SDL3 app. For this first milestone, the PC Engine's video, sound and CD chips come from the Geargrafx core (GPL-3.0, `third_party/geargrafx`). The plan is to replace those with native code over time.
4. **Verification** (`tools/lockstep`): runs the interpreter and the translated build side by side and compares CPU, RAM, VRAM, picture and audio after every frame.

## Building on Windows

Needs the same tools as mof-nx: Visual Studio 2022 Build Tools (C++), LLVM (clang-cl), CMake, Ninja, Python 3, Git.

Folder layout expected in `C:\rondo-nx` (all ignored by git):

- `english\` with the English fan translation (v1.03) extracted: the default language
- `Akumajou Dracula X - Chi no Rondo (Japan) (FABT, FACT)\` with the Japanese original `.cue` and `.bin` files
- `syscard3.pce`
- `tracer\traces\` with the `.rtr` coverage traces

Build:

```
cd C:\rondo-nx
powershell -ExecutionPolicy Bypass -File tools\win\build_pc.ps1
```

The result is `out\pc\rondo-nx.exe` with a `rondo-nx.ini` that points at both discs. Logs go to `out\logs\`.

Language: `language=english` (default) or `language=japanese` in `rondo-nx.ini`, or `--japanese` on the command line. Each language keeps its own save (`saves\rondo-en.sav`, `saves\rondo.sav`). One build covers both: translated blocks are matched by their bytes, so the English patch's changed code and the original code sit side by side.

## Building for Switch

Uses your installed devkitPro (`C:\devkitPro`, devkitA64 + libnx):

```
cd C:\rondo-nx
powershell -ExecutionPolicy Bypass -File tools\win\build_switch.ps1
```

It translates the game code, compiles `platforms\switch\rondo-nx.nro` through devkitPro's MSYS2 make, and assembles an SD card folder in `out\switch\sd\`. Copy the `switch` folder inside it to the root of the SD card:

- `switch/rondo-nx/rondo-nx.nro`
- `switch/rondo-nx/syscard3.pce`
- `switch/rondo-nx/game/` (the English `.cue` and all `.bin` tracks)
- `switch/rondo-nx/game-jp/` (only with `-WithJapanese`; pick it with `language=japanese` in `switch/rondo-nx/rondo-nx.ini`)

Launch from the homebrew menu opened with a title override (hold R while starting a game), so the app gets full memory and the CPU boost clock. Switch controls: Y attack, B jump, X subweapon, + Run, - Select, D-pad or left stick to move. ZL + ZR together (or clicking the right stick) opens the menu; A selects, B goes back.

## Controls

Defaults (all rebindable in the menu under Controls):

| Action | Keyboard | Gamepad | Switch |
| --- | --- | --- | --- |
| Move | Arrow keys | D-pad or left stick | D-pad or left stick |
| Attack (II) | X | X (west) | Y |
| Jump (I) | Z | A (south) | B |
| Subweapon (Up + II) | C | Y (north) | X |
| Run (start/pause) | Enter | Start | + |
| Select | Right Shift | Back | - |

## Menu

Esc or F1 (keyboard), R3 or Guide (gamepad), ZL + ZR or R3 (Switch). The game pauses while it is open.

- Resume, Reset
- Save state / Load state: 5 slots per language, Left/Right picks the slot, the save time is shown
- Controls: rebind keyboard and gamepad (Switch: controller); a key already in use swaps over; Reset to defaults
- Options: Language (English/Japanese, switches discs on the spot), Volume, Aspect (4:3, stretch, or experimental widescreen), Performance
  (corner readout: fps, emulation ms per frame average/max, slow frames, disc reads, and on Switch whether the disc
  is cached in RAM yet), and on PC Pixels (sharp/smooth), Fullscreen, Window size
- Quit

Menu settings and bindings are kept in `saves/settings.ini`. PC hotkeys: F5 save to the current slot, F7 load it,
F11 or Alt+Enter fullscreen, F9 switch translated code / interpreter, Tab fast-forward.

## License

Code in this repository is GPL-3.0, because it builds on the Geargrafx core by Ignacio Sanchez
(`third_party/geargrafx/LICENSE`). Unofficial and fan-made; no affiliation with Konami or Nintendo.

## Making a release

After a Switch build (`tools\win\build_switch.ps1 -NoGameCopy`), run
`powershell -ExecutionPolicy Bypass -File tools\win\package_release.ps1`. It relinks the `.nro` with the version in
`platforms/switch/Makefile` (no re-translation), and writes `out\release\rondo-nx-v<version>-switch.zip` with no game
files or System Card in it.
