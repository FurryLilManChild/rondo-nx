# rondo-nx

A native port of Castlevania: Rondo of Blood (PC Engine Super CD-ROM², 1993), built from your own game files. PC build first, Nintendo Switch next.

No game data is in this repository. The game's code is translated locally from coverage traces of your own disc, and the app reads your disc image and System Card at run time.

## How it works

1. **Coverage tracing** (`tools/tracer`): a patched Geargrafx build logs every piece of code the game runs, including code it loads from disc mid-game.
2. **Translation** (`tools/xlat/gen.py`): turns the traces into C++ blocks. Each instruction runs with its operands baked in, and blocks are matched to the right code overlay by their bytes. Anything not traced falls back to the interpreter.
3. **Runtime** (`runtime/`, `app/`): a standalone SDL3 app. For this first milestone, the PC Engine's video, sound and CD chips come from the Geargrafx core (GPL-3.0, `third_party/geargrafx`). The plan is to replace those with native code over time.
4. **Verification** (`tools/lockstep`): runs the interpreter and the translated build side by side and compares CPU, RAM, VRAM, picture and audio after every frame.

## Building on Windows

Needs the same tools as mof-nx: Visual Studio 2022 Build Tools (C++), LLVM (clang-cl), CMake, Ninja, Python 3, Git.

Folder layout expected in `C:\rondo-nx` (all ignored by git):

- `Akumajou Dracula X - Chi no Rondo (Japan) (FABT, FACT)\` with the `.cue` and `.bin` files
- `syscard3.pce`
- `tracer\traces\` with the `.rtr` coverage traces

Build:

```
cd C:\rondo-nx
powershell -ExecutionPolicy Bypass -File tools\win\build_pc.ps1
```

The result is `out\pc\rondo-nx.exe` with a `rondo-nx.ini` that points at the game files. Logs go to `out\logs\`.

## Controls

| Action | Keyboard | Gamepad |
| --- | --- | --- |
| Move | Arrow keys | D-pad or left stick |
| Attack (I) | X | West (Xbox X) |
| Jump (II) | Z | South (Xbox A) |
| Subweapon (Up + I) | Up + X | North (Xbox Y) |
| Run (start/pause) | Enter | Start |
| Select | Right Shift or Backspace | Back/View |

Hotkeys: F5 save state, F7 load state, F11 or Alt+Enter fullscreen, F2 smooth/sharp pixels, F9 switch between translated code and the interpreter, Tab fast-forward, P pause, Esc quit. Saves (backup RAM) go to `saves\rondo.sav` next to the exe.

The window title shows the frame rate and the share of instructions running as translated code.

## License

Code in this repository is GPL-3.0, because it builds on the Geargrafx core (`third_party/geargrafx/LICENSE`).
