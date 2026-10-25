# Coverage tracer

A patched build of [Geargrafx](https://github.com/drhelius/Geargrafx) (GPL-3.0) that logs every 8 KB bank image the CPU executes from, plus a bitmap of executed instruction starts, plus System Card `cd_read` calls.

- Base commit: `60c7208832f5003b1551bbebf69fae772d7e927d` (Geargrafx 1.8.5)
- Patch: `geargrafx-rondo-trace.patch` (apply with `git apply`)
- Windows build: cross-compiled with MinGW-w64 and SDL3 3.2.24 using `platforms/mingw/Makefile` (edit the SDL path)
- Headless Linux runner: `platforms/headless` (scripted input, screenshots, save states, peek/poke, PC histogram)

## Output

`traces/rondo_trace_<date>_<time>.rtr`: `RTRC`, u32 version, u32 count, then per image: u8 bank, u8 page mask, u16 pad, u64 FNV-1a hash, 8192 bytes of bank data, 1024 bytes of executed-instruction bitmap.

A new image is taken whenever the bytes of the instruction about to run differ from the current image for that bank, so overlays and rewritten code are captured, but changing data next to code is not.

`traces/..._cdread.txt`: one line per `cd_read` call, zero page $F8-$FF.

Read with `tools/rtr.py summary <files>`.

## Findings from headless testing

- The file screen cursor is the flame pair, not the bright text. Cursor index (`$34B8`): 1-3 player slots, 4 NAME ENTRY, 5 NAME CHANGE, 6 FILE DELETE, 7 FILE COPY.
- Name `X-X!V''Q` adds STAGE SELECT, TECHNIC and SOUND TEST to the file menu (verified).
