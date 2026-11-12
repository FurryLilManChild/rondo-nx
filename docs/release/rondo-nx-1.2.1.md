# rondo-nx 1.2.1

A fix-up update to 1.2 for the native Nintendo Switch port of **Castlevania: Rondo of Blood** (PC Engine Super
CD-ROM², 1993). **No game files and no System Card are included**; you need your own.

## Fixes

- **Save states no longer freeze the menu.** The port menu used to re-read every save slot's date from the SD card
  each frame. While a save state was still being written in the background, those reads had to wait for it, so the
  menu could hang for a long time. Slot dates are now read once and updated after each save.
- **Subweapon cheat loads the right graphics.** Swapping subweapons in the Cheats page used to leave the old
  graphics in place, so thrown items could look like the wrong subweapon or a jumbled mix of several. The swap now
  works the same way as picking up a subweapon in the game, so the right graphics load on your next throw.

## Cheats page

- New **Holding now** row shows the subweapon you currently have.
- **Replace with** picks a subweapon with Left/Right; press **A** to swap it in. The swap happens straight away, and
  **Holding now** updates right away to match.

## Widescreen (still experimental)

- **The HUD follows the level's left edge.** In open areas the health bar, hearts, subweapon box and score move out
  to the left edge of the widescreen picture. Where the room ends and the side area is black, the HUD sits against
  the room's edge instead, so it never overlaps the black area. It stays put after opening and closing the port menu.
- **The subweapon icon moves with the HUD.** It used to stay behind in the old 4:3 position.
- **The side areas open and close smoothly.** In some rooms (for example the room at the top of the first stairs
  in Stage 4), the side area used to snap from black to full scenery, or back, all at once. It now opens and closes
  one pixel at a time with the camera, as in the rest of the game.

The known widescreen limitations from 1.2 still apply: some sprites can leave stray colored bands or edge glitches in
the side areas, some enemies appear partway into the side areas, and scripted camera moves can briefly show the wrong
scenery at the sides.

## Install or update

- **New install:** extract `rondo-nx-v1.2.1-switch.zip` to the root of your SD card, then add `syscard3.pce` and your
  disc as described in 1.0.
- **Updating from 1.0, 1.1 or 1.2:** copy only `switch/rondo-nx/rondo-nx.nro` over the old one. Your System Card,
  disc, game save, save states, settings and cheat choices stay as they are.

## Known issues

- Not played to the end on the Switch yet.
- Start it with title takeover (hold **R** while opening any game). From the album applet it still runs, but
  without the CPU boost clock and with the disc read from the SD card instead of cached in memory.
- Cheats are untested with Maria.
- Widescreen limitations as listed above.

## Reporting problems

Open an issue on GitHub with where in the game it happened, what you saw, your Atmosphère version, and whether you
were playing in English or Japanese. For widescreen problems, a photo of the screen and the stage and room are the
most useful, and a save state from just before the spot helps even more (they're in `switch/rondo-nx/saves/`).

## Credits

Geargrafx (Ignacio Sanchez and contributors), the English fan translation team, devkitPro and libnx.
No affiliation with Konami or Nintendo. Castlevania is a trademark of Konami.

GPL-3.0. Source: this repository.
