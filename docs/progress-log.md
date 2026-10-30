# Progress log

Newest first.

## 2026-10-25 (midday): English, and self-modifying code

- English (fan translation v1.03) is now the default language; the Japanese original stays selectable
  (`language=` in the ini, `--japanese`). Separate saves per language. One build covers both discs.
- The English patch adds a self-modifying subtitle routine (bank 6C, around `$9B36`: it rewrites the operands of
  its own LDA/STA each pass). The Japanese game also had one (a jump table patched into a `JMP (abs,X)`).
- Tracer v2 records operand bytes that change while the opcode stays (`.rtr` version 2, volatile bitmaps) and no
  longer snapshots a new image for them (English boot: 2,225 images before, 15 after).
- Translator: instructions with rewritten operands read them live from memory (no baked constants); constant
  stores into the code's own page are detected statically too; blocks match with a byte mask that skips those
  bytes. Rewritten opcodes are left to the interpreter.
- English coverage: boot and intro, file select, name entry, menus (Tactics, Sound Test), every stage, alternate and
  boss version, and a real game over (27 traces in `tracer/traces/english`). The fan patch's name check differs:
  the stage-select name does not unlock the menu entry in English; stages were forced through `$81` instead.
- Lockstep, English: boot, menus, name entry, stages 0, 3, 9, boss 10, game over: zero differences. Japanese
  stages rechecked: zero. Cold boots need the same power-on seed in both cores (`GG_SEED`, added to the core for
  testing); with it, four seeds give zero differences. The one mismatch seen was the two cores seeding a second
  apart.

## 2026-10-25 (overnight): first translated build

- Coverage: Mark's sessions (Stages 0-2 with bosses, the ending, Maria, Item Crashes, game over) plus 22 scripted runs
  (every stage, alternate and boss version, forced through zero page `$81`). About 52,000 distinct instructions.
- Translator (`tools/xlat/gen.py`): 16,541 blocks, 60,105 instructions, 42 C++ files. Blocks keyed by (bank, offset,
  bytes); work RAM routines (self-patching block transfers) and BRK/block-transfer opcodes stay interpreted.
- Execution: `HuC6280::XlatInstruction` runs each instruction with baked-in operands and the exact bookkeeping of
  `RunInstruction`; `GeargrafxCore::XlatPost` clocks the hardware after every instruction. Timing is unchanged.
- Verification (`tools/lockstep`): boot (3,000 frames) and all 22 stage/boss scripts (16,000 frames each) match the
  interpreter exactly: CPU registers, all RAM banks, VRAM, picture and audio, every frame. Same result with
  GG_DISABLE_DISASSEMBLER and -O3. The only differences seen are at frame 0 of a fresh boot, from the core's
  time-seeded power-on palette, before a save state loads.
- In play, 99.8-99.9% of executed instructions run as translated code.
- Speed: translated and interpreted run at the same speed (about 480 fps on the cloud x86 with the fast config).
  Callgrind: about 58% of time is per-instruction hardware clocking, mostly the HuC6260/HuC6270 pixel loop. The CPU
  side is about 15%. Real speed gains need the native renderer (scanline-based), not more CPU work.
- PC app (`app/main.cpp`, SDL3): keyboard and gamepad, auto-boot past the System Card, saves, save states,
  fullscreen, fast-forward, live translated-instruction share in the title. Tested under Wine: boots, 60 fps, input.
- Switch app (`app/switch_main.cpp`, libnx): framebuffer video, audout with 44.1 to 48 kHz resampling, pad input,
  CPU boost mode. Syntax-checked against libnx headers; not compiled yet (devkitPro is not reachable from the cloud
  session). `platforms/switch/Makefile` and `tools/win/build_switch.ps1` build it with Mark's devkitPro.
- Bridge note: overwriting an existing file on Mark's PC through the device bridge reports success but keeps the old
  contents. Write under a new name and rename on the PC instead.

### Next

- Mark: run the prebuilt (`out\prebuilt\rondo-nx.exe`), then `tools\win\build_pc.ps1`, then `build_switch.ps1`.
- Measure Switch frame time. If tight: smaller framebuffer scaled by the compositor, LTO, then the native renderer.
- English: the fan translation as a second data track (needs its own trace pass or a static scan of changed code).
- Replace hardware layers with native code over time (renderer first), then drop the System Card via HLE.

## 2026-10-30: release 0.5.0 (test release) prepared

- Version 0.5.0 in the homebrew menu (`APP_VERSION`); the .nacp now rebuilds when the Makefile changes.
- `tools/win/package_release.ps1`: relinks the tested build with the new version (no re-translation), makes
  `out/release/rondo-nx-v0.5.0-switch.zip` (NRO, readme, game-folder note, GPL licence; no disc, BIOS or saves, checked),
  prints its SHA-256.
- README: status, download/install, problem reports, release steps. Release notes: `docs/release/rondo-nx-0.5.0.md`.

