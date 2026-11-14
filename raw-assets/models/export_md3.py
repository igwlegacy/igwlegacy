"""Export a .blend as an idTech 3 MD3 with SomaZ's id Tech 3 add-on.

The export goes through the MD3 exporter of the "Import id Tech 3 BSP"
add-on (https://github.com/SomaZ/Blender_BSP_Importer), loaded straight
from a local checkout so that it needs no installation.

Usage:
    blender --factory-startup model.blend --python export_md3.py -- \
        /path/to/Blender_BSP_Importer \
        ../../gamedir/models/<dir>/<name>.md3

Blender has to run with a window: the add-on compiles GPU shaders when it
is imported, which fails in --background mode. The window closes by
itself once the export has finished or failed, and the exit code tells
which. --factory-startup keeps the user's saved preferences untouched.

Conventions:
    * 1 Blender unit equals 1 Quake unit, +Z is up, the object origin is
      the model origin. Coordinates must stay within -512..511.99.
    * Every visible object is exported in world space as a single static
      frame; empties become tags.
    * Surfaces are merged per material and the material name is written
      as the surface shader, so it must be the shader path without
      extension. Everything after the first "." of a material name is
      dropped by the add-on.
    * The output path should contain "/models/": the add-on stores the
      part from there on as the model name inside the MD3.
"""

import os
import sys
import traceback

import addon_utils
import bpy

ADDON_MODULE = "import_bsp"


def export(addon_checkout, out_path):
    # The add-on is a plain package inside the checkout; making it
    # importable lets addon_utils enable it without an installation.
    sys.path.insert(0, os.path.abspath(addon_checkout))
    # default_set registers the add-on's preferences entry, which its
    # register() reads.
    addon_utils.enable(ADDON_MODULE, default_set=True, handle_error=None)
    result = bpy.ops.export_scene.id3_md3(
        filepath=os.path.abspath(out_path),
        only_selected=False,
        individual=False,
        start_frame=0,
        end_frame=0,
        preset='MATERIALS',
        # 999 vertices and 32 surfaces at most, safe for Quake 3 era engines.
        limits='LEGACY',
    )
    if result != {'FINISHED'}:
        raise RuntimeError(f"MD3 export failed: {result}")
    print(f"exported {out_path}")


if __name__ == "__main__":
    args = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    exit_code = 1
    try:
        if len(args) != 2:
            raise SystemExit("usage: blender --factory-startup model.blend "
                             "--python export_md3.py -- ADDON_CHECKOUT OUT.md3")
        export(*args)
        exit_code = 0
    except BaseException:
        traceback.print_exc()
    finally:
        # Close the window that the GPU requirement forces us to open.
        # wm.quit_blender always exits with 0, so leave directly to report
        # failures; there is nothing to save in this throwaway session.
        sys.stdout.flush()
        sys.stderr.flush()
        os._exit(exit_code)
