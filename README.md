# Iron Grip: Warlord Legacy
As long as <https://github.com/reigw/reigw> hasn't finished, `IGWL` will be
rather minimal and focus on modifying assets and monkey patching existing
binaries to start with the low-hanging fruits first.

# Build
Put the original IGW `gamex86.dll`, `cgamex86.dll`, `uix86.dll`, and `igwarlord.exe` in
`./input-binaries/`. A native C17 compiler, `idt3tc`, and Blender must be
available on `PATH`. Models are exported through the vendored add-on in
`./model-converter/`, so fetch the submodule first.

```sh
git submodule update --init
pixi run build
```

The output in `./dist/` mirrors an IGW installation: the patched
`igwarlord.exe` as `igwlegacy.exe` at the top and `base/` with
`gamex86.dll`, `cgamex86.dll`, `uix86.dll`, and
`igwlegacy.pk3`, a plain ZIP archive of the contents of `./gamedir/`
plus the assets generated from `./raw-assets/`: textures converted with
`idt3tc` and Blender models exported as static MD3s by
[`model-converter/export_md3.py`](model-converter/export_md3.py). The
`TEXTURE_RULES` and `MODEL_RULES` in `dodo.py` map game paths to their sources.
All four binaries pass through the monkey patcher. The executable defaults `r_forceFog` to `1` and `r_forceFogDensity` to `0`; existing settings still take precedence. It forces `r_useShaders` to `1` and `r_heathazedistort` to `0` regardless of saved or runtime settings, and skips the intro video at startup. The UI patch removes the
main-menu promotional panel and its update request, and automatically enters the game once loading finishes without requiring a `PLAY!` click.
Archive paths start directly with `scripts/`, `sound/`, etc. Sorted entries,
fixed timestamps and permissions, and a fixed compression level make the
archive reproducible for unchanged contents in the same Pixi environment.
