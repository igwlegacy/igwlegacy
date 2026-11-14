# Iron Grip: Warlord Legacy
As long as <https://github.com/reigw/reigw> hasn't finished, `IGWL` will be
rather minimal and focus on modifying assets and monkey patching existing
.dll's to start with the low-hanging fruits first.

# Build
Put the original IGW `gamex86.dll` and `cgamex86.dll` in `./input-dlls/`.
Meson, Ninja, and a native C17 compiler must be available on `PATH`.

```sh
pixi run build
```

This compiles `monkey-patcher/` and assembles `./dist/` with everything from
`./gamedir/` copied verbatim, preserving its directory structure, plus the
patched `gamex86.dll` and `cgamex86.dll` directly in `./dist/`. Each run replaces
the previous output after patching succeeds.
