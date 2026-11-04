# rondo-nx 1.2

An update to 1.1 for the native Nintendo Switch port of **Castlevania: Rondo of Blood** (PC Engine Super CD-ROM²,
1993). **No game files and no System Card are included**; you need your own.

## New: Widescreen (experimental)

**Options > Aspect** now has a third setting, **Widescreen**, next to 4:3 and Stretch. It's off unless you pick it.

During gameplay, Widescreen shows extra real scenery on both sides of the screen for a true 16:9 picture, instead of
stretching the image:

- The extra scenery comes from the level itself, using the same scroll positions the game uses for each line, so
  parallax layers line up with the original picture.
- Enemies, items, candles and projectiles are drawn in the side areas too, so most things no longer pop in at the
  old 4:3 border.
- Rooms that don't scroll, and the edges of each section, show black past the room's edge, using the game's own
  camera limits. The next room and hidden areas stay hidden until the game would normally show them.
- Title screen, menus, the map and cutscenes stay 4:3 with black side bars.
- The port menu stays in the 4:3 middle of the screen.

It's marked experimental because it isn't finished yet. Known limitations:

- Some sprites can still leave stray colored bands or edge glitches in the side areas. It's largely fixed, but you
  may still notice it here and there.
- Some enemies still appear partway into the side areas instead of at the outer edge, because the game only creates
  them once they're close to the old 4:3 edge.
- Scripted camera moves (some cutscenes and boss intros) can briefly show the wrong scenery at the sides.

Reports with the stage and room help a lot (see below).

## Install or update

- **New install:** extract `rondo-nx-v1.2-switch.zip` to the root of your SD card, then add `syscard3.pce` and your
  disc as described in 1.0.
- **Updating from 1.0 or 1.1:** copy only `switch/rondo-nx/rondo-nx.nro` over the old one. Your System Card, disc,
  game save, save states, settings and cheat choices stay as they are.

## Known issues

- Not played to the end on the Switch yet.
- Start it with title takeover (hold **R** while opening any game). From the album applet it still runs, but
  without the CPU boost clock and with the disc read from the SD card instead of cached in memory.
- Cheats are untested with Maria.
- Widescreen limitations as listed above.

## Reporting problems

Open an issue on GitHub with where in the game it happened, what you saw, your Atmosphère version, and whether you
were playing in English or Japanese. For widescreen problems, a photo of the screen and the stage and room are the
most useful.

## Credits

Geargrafx (Ignacio Sanchez and contributors), the English fan translation team, devkitPro and libnx.
No affiliation with Konami or Nintendo. Castlevania is a trademark of Konami.

GPL-3.0. Source: this repository.
