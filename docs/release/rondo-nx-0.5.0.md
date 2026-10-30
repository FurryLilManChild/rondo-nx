# rondo-nx 0.5.0 (test release)

A native Nintendo Switch port of **Castlevania: Rondo of Blood** (PC Engine Super CD-ROM², 1993). The game's code is
translated to C++ and compiled for the Switch; the PC Engine's video, sound and CD hardware come from the Geargrafx
core. Unofficial and fan-made. **No game files and no System Card are included**; you need your own.

This is a **test release**: it has been played on a V1 Switch (Atmosphère) and runs well, but it has not been played
to the end on the Switch. Please report anything that goes wrong.

## What works

- Every stage, alternate stage and boss was traced and translated, and the translated code was checked frame by
  frame against the emulator (CPU, memory, video and sound) with no differences.
- English (the v1.03 fan translation) by default; Japanese too if you add the original disc.
- Port menu (**ZL + ZR** together, or click the right stick): resume, reset, 5 save-state slots per language,
  controller remapping, language, volume, aspect ratio (4:3 or stretch), performance readout.
- Saves, with an automatic daily backup of the save file. Reset keeps your save.

## You need

- A Switch with custom firmware (Atmosphère).
- The System Card 3.0 BIOS, saved as `syscard3.pce`.
- Castlevania: Rondo of Blood with the English fan translation v1.03 applied, as a `.cue` file with its `.bin` tracks.
- Optional: the Japanese original disc (`.cue` and `.bin`), for the Japanese option.

## Install

1. Extract `rondo-nx-v0.5.0-switch.zip` to the root of your SD card. It makes `switch/rondo-nx/`.
2. Copy your System Card to `switch/rondo-nx/syscard3.pce`.
3. Copy the English `.cue` and all of its `.bin` tracks into `switch/rondo-nx/game/`.
4. Optional: copy the Japanese disc into `switch/rondo-nx/game-jp/`.
5. Hold **R** while opening any game to get the homebrew menu with full memory, then start **rondo-nx**.

## Controls

| Action | Switch |
| --- | --- |
| Move | D-pad or left stick |
| Attack (II) | Y |
| Jump (I) | B |
| Subweapon (Up + II) | X |
| Run (start/pause) | + |
| Select | - |
| Port menu | ZL + ZR, or click the right stick |

All buttons can be remapped in the port menu under Controls.

## Known issues

- Not played to the end on the Switch yet.
- Start it with title takeover (step 5). From the album applet it still runs, but without the CPU boost clock and
  with the disc read from the SD card instead of cached in memory.
- With title takeover the port uses the Switch's CPU boost clock while it runs and puts it back when you quit.

## Reporting problems

Open an issue on GitHub with where in the game it happened, what you saw, your Atmosphère version, and whether you
were playing in English or Japanese.

## Credits

Geargrafx (Ignacio Sanchez and contributors), the English fan translation team, devkitPro and libnx.
No affiliation with Konami or Nintendo. Castlevania is a trademark of Konami.

GPL-3.0. Source: this repository.
