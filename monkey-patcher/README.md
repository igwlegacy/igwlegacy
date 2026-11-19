# igw-monkey-patcher

A native command line tool written in C17 that patches Iron Grip: Warlord's Windows game DLLs and executable on disk. Build it with Meson on Linux or Windows. It needs only the C standard library; Wine, a disassembler, the game source, and a Windows cross compiler are not needed to run it.

Each functionality has its own C file in [patches/](../patches/) at the repository root, such as [screen_shake.c](../patches/screen_shake.c), [damage_kick.c](../patches/damage_kick.c), and [valuable_markers.c](../patches/valuable_markers.c). It supports changing CVAR defaults, forcing CVAR values, replacing instruction bytes, injecting runtime features, and dropping imported DLLs. The valuable marker patch permanently displays a jewelry model at every map-defined valuable spawn position, including empty ones. Outputs are separate DLL or EXE files. The game does not need a loader or a background patching process to use them.

## Build and use

Run these commands from the repository root:

```sh
meson setup build
meson compile -C build

build/monkey-patcher/igw-monkey-patcher --list
build/monkey-patcher/igw-monkey-patcher --dry-run \
  ./gamex86.dll \
  ./cgamex86.dll \
  ./uix86.dll \
  ./igwarlord.exe \
  ./igwarlord-server.exe

build/monkey-patcher/igw-monkey-patcher \
  ./gamex86.dll \
  ./cgamex86.dll \
  ./uix86.dll \
  ./igwarlord.exe \
  ./igwarlord-server.exe
```

`igwarlord-server.exe` stands for the dedicated server's `server/igwarlord.exe`, which differs from the client's. By default, this creates `gamex86.patched.dll`, `cgamex86.patched.dll`, `uix86.patched.dll`, `igwarlord.patched.exe`, and `igwarlord-server.patched.exe` beside the inputs. Both executables no longer import `steam_api.dll`: every Steam function resolves to a stub returning 0, which the game already handles as Steam not running. The executable defaults `r_forceFog` to `1` and `r_forceFogDensity` to `0`; saved settings and later changes still take precedence. It forces `r_useShaders` to `1` and `r_heathazedistort` to `0` regardless of saved or runtime settings, skips the intro video at startup, and mirrors all console output to stdout. The UI patch removes the main-menu promotional panel and its update request. Use `--output-dir DIR` to write the original basenames into an existing directory instead:

```sh
mkdir -p patched
build/monkey-patcher/igw-monkey-patcher --output-dir patched \
  ./gamex86.dll \
  ./cgamex86.dll \
  ./uix86.dll \
  ./igwarlord.exe \
  ./igwarlord-server.exe
```

Inputs are identified by their contents, so renamed input files work too. Existing outputs are never overwritten, including when an output path aliases an input. All inputs are validated and patched in memory before any output is created. A write failure removes outputs created by that invocation; a process crash or power loss can still leave files behind. `--dry-run` performs the same input and patch validation without creating outputs. `--` ends option parsing.
