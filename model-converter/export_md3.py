"""Export the opened .blend as a static idTech 3 MD3.

The export goes through the MD3 exporter of SomaZ's "Import id Tech 3 BSP"
Blender add-on, vendored as the Blender_BSP_Importer submodule next to this
script. Run it headless:

    blender --background --factory-startup model.blend \
        --python-exit-code 1 --python export_md3.py -- OUT.md3

--factory-startup keeps the user's preferences and add-ons out of the
export, and --python-exit-code makes errors fail the Blender process.

Conventions:
    * 1 Blender unit equals 1 Quake unit, +Z is up, the world origin is the
      model origin. Coordinates must stay within -512..511.99.
    * Every visible mesh object is exported in world space as a single
      static frame at the scene's current frame; visible empties become tags.
    * Surfaces are merged per material and the material name is written as
      the surface shader, so it must be the shader path without extension.
      Everything after the first "." of a material name is dropped.
    * At most 999 vertices per surface and 32 surfaces, the limits of Quake
      3 era engines.
    * OUT.md3 should contain "/models/": the part from there on is stored as
      the model name inside the MD3.
"""

import sys
import types
from pathlib import Path

import bpy

ADDON_PACKAGE = Path(__file__).resolve().parent / "Blender_BSP_Importer" / "import_bsp"
# Legacy limits of the add-on's exporter: vertices per surface, surfaces.
MAX_SURFACE_VERTICES = 1000
MAX_SURFACES = 32


def _load_md3_module():
    """Import the add-on's MD3 module without running the add-on's __init__.

    The package __init__ imports the whole add-on, including a sky preview
    that compiles GPU shaders at import time and so fails in background
    mode. A bare package object with the right search path lets the MD3
    module and its few dependencies import on their own instead.
    """
    if not (ADDON_PACKAGE / "MD3.py").is_file():
        raise RuntimeError(
            f"{ADDON_PACKAGE} is missing, run: git submodule update --init"
        )
    package = types.ModuleType(ADDON_PACKAGE.name)
    package.__path__ = [str(ADDON_PACKAGE)]
    sys.modules[ADDON_PACKAGE.name] = package
    from import_bsp import MD3

    return MD3


def export(out_path):
    md3 = _load_md3_module()
    scene = bpy.context.scene
    # Hidden objects are construction or reference helpers, not model parts.
    objects = [obj for obj in scene.objects if obj.visible_get()]
    if not any(obj.type == "MESH" for obj in objects):
        raise RuntimeError("no visible mesh objects to export")
    out_path.parent.mkdir(parents=True, exist_ok=True)
    ok, status = md3.ExportMD3(
        out_path.as_posix(),
        objects,
        [scene.frame_current],
        False,  # world space instead of per-object local space
        True,  # merge surfaces per material
        MAX_SURFACE_VERTICES,
        MAX_SURFACES,
    )
    if not ok:
        raise RuntimeError(f"MD3 export of {bpy.data.filepath} failed: {status}")
    print(f"exported {out_path}")


if __name__ == "__main__":
    args = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    if len(args) != 1:
        raise SystemExit(
            "usage: blender --background --factory-startup model.blend "
            "--python-exit-code 1 --python export_md3.py -- OUT.md3"
        )
    export(Path(args[0]).resolve())
