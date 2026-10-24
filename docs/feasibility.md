# rondo-nx feasibility scan

Scan date: 2026-10-24. Source files live in `C:\rondo-nx` on Mark's PC and are never committed.

## Inputs

| File | Notes |
| --- | --- |
| Akumajou Dracula X - Chi no Rondo (Japan) (FABT, FACT) | Redump-named bin/cue, 22 tracks. Track 02 and Track 22 are data (MODE1/2352), the rest CD audio. |
| syscard3.pce | Super CD-ROM² System Card 3.0, SHA1 `79f5ff55dd10187c7fd7b8daab0b3ffbd1f56a2c` |
| Akumajou Dracula X - Chi No Rondo (English v1.03).7z | Fan translation, full patched disc image (data track stored as MODE1/2048) |
| Castlevania - The Dracula X Chronicles (USA).iso | PSP UMD image |
| Castlevania - Rondo of Blood (USA) (TGX) (Virtual Console).wad | Wii VC. Not analyzed yet; kept as a backup reference. |

## Disc layout

- Data track (Track 02): 225-sector pregap, then 10,149 sectors (about 20.8 MB) of game data. Only 227 sectors are empty.
- Track 22 is the usual second copy of the data track.
- Tracks 01 and 03 to 21 are Red Book audio (music and voiced cutscenes). These become plain audio files in the port.

## Boot chain

- Data sector 1 holds the IPL header: load 1 record from sector 2 to `$3000`, jump to `$3000`, MPR2-6 = 0,1,2,3,4.
- The loader at `$3000` calls `ex_getver` (`$E05A`).
  - System Card 3.0+: calls `ex_memopen` (`$E0DE`) to find Super RAM, then walks a load table at `$312F`.
  - Older cards: loads 20 sectors from sector `$23` instead. This is the "Dracula Peke" downgrade mode.
- Load table (Super CD path): base LBA `$000F75` (data sector `$43`), records:
  - map banks +1/+2/+0A, load 10 sectors from base+0
  - map banks +0D/+0E/+0F, load 12 sectors from base+0A
  - map banks +10/+11/+11, load 8 sectors from base+16
  - then `JMP $4000`
- So the boot image is about 30 sectors (about 60 KB). Everything after that is loaded by the game's own code on demand, which means **disc overlays are confirmed** and a pure static scan of the disc will not find all code.

## System Card usage

A byte scan for `JSR $E0xx` across the data track (overcounts, since graphics data can look like code) shows calls into most of the BIOS jump table. The heavy hitters:

- `cd_read` (`$E009`), `cd_pause` (`$E018`), `cd_fade`, `cd_seek`, `cd_exec`
- ADPCM: `ad_reset`, `ad_read`, `ad_stop`, `ad_cplay`, `ad_stat`
- Display/IRQ: `ex_rcroff`, `ex_setrcr`, `ex_vsync`, `ex_sprdma`, `ex_getfnt`
- Math: `ma_mul8u`, `ma_mul8s`
- `psg_driver` / `psg_bios`
- `bm_*` backup RAM (save data)

Plan: reimplement these as native C functions (HLE) instead of running the BIOS ROM. The BIOS file is then only needed as a test reference.

## English sources

### PSP (Dracula X Chronicles)
- `res/pce/arc/DRACX_02.DAT` is the original data track, **byte-identical** to Mark's disc for all 10,072 sectors except the first two (header zeroed).
- Konami's English is layered on top by the PSP emulator (`res/pce/PCE.ELF`):
  - `res/pce/loc/USA.BIN` (and FR/GE/IT/SP): TIM2 images, i.e. pre-rendered text graphics swapped in at runtime.
  - `res/pce/eng/DEMO_*.BIN`: `MSTR1.00` cutscene/subtitle scripts.
  - `res/pce/eng/bgm/*.at3`: English-dubbed cutscene audio (ATRAC3).
- Usable, but the swap rules live inside `PCE.ELF`, so we would have to reverse that too. Bigger job, best quality (official script and dub).

### Fan translation v1.03
- Same disc layout as the original. 1,660 of 10,148 data sectors changed, in 130 runs, including the boot/code area.
- In-place patch, so it fits the port with no extra machinery: recompile it as a second data track and offer it as a language option.

## Verdict

Feasible, and smaller than Mirror of Fate in code terms, but the work is in rebuilding hardware rather than translating code.

| Area | Difficulty | Notes |
| --- | --- | --- |
| CPU code translation | Low | 8-bit 65C02-family, small code, simple instruction set |
| Overlay discovery | Medium | Needs coverage playthrough; interpreter fallback covers anything missed |
| System Card HLE | Medium | Well-documented jump table |
| Video (VDC/VCE) | Medium-high | Must match mid-frame raster effects exactly |
| Audio (PSG + ADPCM + CD-DA) | Medium | CD-DA is trivial; PSG and ADPCM need accurate emulation |
| Switch shell | Low | Reuse mof-nx packaging and input |
| English | Low (fan) / High (official) | Start with fan translation, consider PSP assets later |

## Plan

1. Coverage tracer: a logging emulator build Mark plays through, recording every executed address with its bank mapping.
2. Recompiler: HuC6280 to C, keyed by (bank, address), with an interpreter fallback for code the trace never hit.
3. Runtime: native VDC/VCE renderer, PSG, ADPCM, CD audio streaming, System Card HLE, save RAM.
4. PC build first, then Switch using the mof-nx shell.
5. English: fan translation data track as a second language.
