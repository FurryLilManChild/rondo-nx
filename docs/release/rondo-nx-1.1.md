# rondo-nx 1.1

A small update to 1.0 for the native Nintendo Switch port of **Castlevania: Rondo of Blood** (PC Engine Super
CD-ROM², 1993). **No game files and no System Card are included**; you need your own.

## New: Cheats

A new **Cheats** page in the port menu (**ZL + ZR**, or click the right stick):

- **Infinite health**: your health refills as soon as you take damage. Falling into a pit still costs a life.
- **Infinite hearts**: hearts stay at 99, so subweapons and Item Crashes are always available.
- **Subweapon**: pick Dagger, Axe, Holy Water, Cross, Bible, Stopwatch or Key with Left/Right, then press A to get
  it. It's handed over as soon as you're back in gameplay, and you can still pick up others in the stage as usual.

The cheats only act during gameplay, and your choices are remembered between sessions. They were made and tested
with Richter; Maria hasn't been tested with them yet.

## Fixed

- **Save states could freeze the game while saving** (reported at up to about 2 minutes). A state is now copied in
  well under a frame and written to the SD card in the background, so the game keeps running. The menu shows
  "Saving..." and then "Saved to slot N" when the file is written. Loading a state now reads the file in one go.

## Install or update

- **New install:** follow the steps in 1.0: extract `rondo-nx-v1.1-switch.zip` to the root of your SD card,
  then add `syscard3.pce` and your disc.
- **Updating from 1.0:** copy only `switch/rondo-nx/rondo-nx.nro` over the old one. Your System Card, disc, game
  save, save states and settings stay as they are.

## Known issues

- Not played to the end on the Switch yet.
- Start it with title takeover (hold **R** while opening any game). From the album applet it still runs, but
  without the CPU boost clock and with the disc read from the SD card instead of cached in memory.
- Cheats are untested with Maria.

## Reporting problems

Open an issue on GitHub with where in the game it happened, what you saw, your Atmosphère version, and whether you
were playing in English or Japanese.

## Credits

Geargrafx (Ignacio Sanchez and contributors), the English fan translation team, devkitPro and libnx.
No affiliation with Konami or Nintendo. Castlevania is a trademark of Konami.

GPL-3.0. Source: this repository.
