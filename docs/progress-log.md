# Progress log

Newest first.

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
