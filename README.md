# Iron Grip: Warlord Legacy
As long as <https://github.com/reigw/reigw> hasn't finished, `IGWL` will be
rather minimal and focus on modifying assets and monkey patching existing
binaries to start with the low-hanging fruits first.

# Build
Put the original IGW `gamex86.dll`, `cgamex86.dll`, `uix86.dll`, and `igwarlord.exe` in
`./input-binaries/`. A native C17 compiler must be available on `PATH`.

```sh
pixi run build
```

The output in `./dist/` contains `gamex86.dll`, `cgamex86.dll`, `uix86.dll`, and `igwarlord.exe`
alongside `igwlegacy.pk3`, a plain ZIP archive of the contents of `./gamedir/`.
All four binaries pass through the monkey patcher. The executable defaults `r_forceFog` to `1` and `r_forceFogDensity` to `0`; existing settings still take precedence. The UI patch removes the
main-menu promotional panel and its update request.
Archive paths start directly with `scripts/`, `sound/`, etc. Sorted entries,
fixed timestamps and permissions, and a fixed compression level make the
archive reproducible for unchanged contents in the same Pixi environment.
