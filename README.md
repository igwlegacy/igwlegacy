# Iron Grip: Warlord Legacy
As long as <https://github.com/reigw/reigw> hasn't finished, `IGWL` will be
rather minimal and focus on modifying assets and monkey patching existing
binaries to start with the low-hanging fruits first.

# Build
Put the original IGW `gamex86.dll`, `cgamex86.dll`, and `uix86.dll` in
`./input-dlls/`. A native C17 compiler must be available on `PATH`.

```sh
pixi run build
```

The output in `./dist/` contains `gamex86.dll`, `cgamex86.dll`, and `uix86.dll`
alongside `igwlegacy.pk3`, a plain ZIP archive of the contents of `./gamedir/`.
All three DLLs pass through the monkey patcher. The UI patch removes the
main-menu promotional panel and its update request.
Archive paths start directly with `scripts/`, `sound/`, etc. Sorted entries,
fixed timestamps and permissions, and a fixed compression level make the
archive reproducible for unchanged contents in the same Pixi environment.
