# igw-monkey-patcher

A native command line tool written in C17 that patches Iron Grip: Warlord's Windows game DLLs on disk. Build it with Meson on Linux or Windows. It needs only the C standard library; Wine, a disassembler, the game source, and a Windows cross compiler are not needed to run it.

Each functionality has its own C file in [patches/](../patches/) at the repository root, such as [run_pitch.c](../patches/run_pitch.c), [screen_shake.c](../patches/screen_shake.c), and [damage_kick.c](../patches/damage_kick.c). It supports changing CVAR defaults, forcing CVAR values, replacing instruction bytes, and making a verified void function return immediately. Outputs are separate DLL files. The game does not need a loader or a background patching process to use them.

## Build and use

```sh
meson setup build
meson compile -C build

build/igw-monkey-patcher --list
build/igw-monkey-patcher --dry-run \
  ./gamex86.dll \
  ./cgamex86.dll \
  ./uix86.dll

build/igw-monkey-patcher \
  ./gamex86.dll \
  ./cgamex86.dll \
  ./uix86.dll
```

By default, this creates `gamex86.patched.dll`, `cgamex86.patched.dll`, and `uix86.patched.dll` beside the inputs. The UI DLL currently has no patch rules and is written unchanged after validation. Use `--output-dir DIR` to write the original basenames into an existing directory instead:

```sh
mkdir -p patched
build/igw-monkey-patcher --output-dir patched \
  ./gamex86.dll \
  ./cgamex86.dll \
  ./uix86.dll
```

Inputs are identified by their contents, so renamed input files work too. Existing outputs are never overwritten, including when an output path aliases an input. All inputs are validated and patched in memory before any output is created. A write failure removes outputs created by that invocation; a process crash or power loss can still leave files behind. `--dry-run` performs the same input and patch validation without creating outputs. `--` ends option parsing.
