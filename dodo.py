"""Build the native patcher and assemble the game distribution."""

from pathlib import Path
import shutil
import stat
import subprocess
import tempfile
import zipfile

from doit.tools import config_changed

ROOT = Path(__file__).resolve().parent
PATCHER_BUILD = ROOT / "build"
PATCHER = PATCHER_BUILD / "monkey-patcher" / "igw-monkey-patcher"
INPUT_BINARIES = [
    ROOT / "input-binaries" / name
    for name in ("gamex86.dll", "cgamex86.dll", "uix86.dll", "igwarlord.exe")
]
DIST = ROOT / "dist"
ASSET_BUILD = ROOT / "build" / "assets"
# Use None for the size to convert formats while preserving source dimensions.
TEXTURE_RULES = {
    "textures/ui/mmbg1.dds": ("wallpaper.png", "2048x2048"),
    "textures/ui/mmbg1ws.dds": ("wallpaper.png", "2048x2048"),
    "textures/ui/mmbottomedge1.dds": ("nothing.png", "256x256"),
    "textures/ui/mmcorner.dds": ("nothing.png", "256x256"),
    "textures/ui/mmcorner.dds": ("nothing.png", "256x256"),
    "textures/ui/mmrightborder.dds": ("nothing.png", "256x256"),
    "textures/ui/mmsideedge1.dds": ("nothing.png", "256x256"),
    "textures/ui/mmsideedge2.dds": ("nothing.png", "256x256"),
    "textures/ui/mmtopedge1.dds": ("nothing.png", "256x256"),
    "textures/ui/mmtopleftpart.dds": ("nothing.png", "256x256"),
    "textures/ui/mmtoplogo.dds": ("logo.png", "512x512"),
    "textures/ui/mmtoplogoSE.dds": ("logo.png", "512x512"),
    # The shader references the .tga, so ship it alongside the .dds.
    "models/mapobjects/item_highlight/item_highlight.tga": (
        "models/item_highlight.png",
        None,
    ),
    "models/mapobjects/item_highlight/item_highlight.dds": (
        "models/item_highlight.png",
        None,
    ),
}

# Blender sources below raw-assets/ exported as static MD3s.
MODEL_RULES = {
    "models/mapobjects/item_highlight/item_highlight.md3": "models/item_highlight.blend",
}
MD3_EXPORTER = ROOT / "model-converter" / "export_md3.py"
MD3_ADDON = ROOT / "model-converter" / "Blender_BSP_Importer" / "import_bsp"
GENERATED_ASSETS = [*TEXTURE_RULES, *MODEL_RULES]

DOIT_CONFIG = {"default_tasks": ["dist"]}


def _build_patcher():
    # Meson and Ninja track source changes and avoid unnecessary compilation.
    if not (PATCHER_BUILD / "meson-private" / "coredata.dat").is_file():
        subprocess.run(
            ["meson", "setup", str(PATCHER_BUILD), str(ROOT)], check=True
        )
    subprocess.run(["meson", "compile", "-C", str(PATCHER_BUILD)], check=True)


def _convert_texture(source, size, output):
    # Generated textures stay outside gamedir, which remains a source tree.
    output.parent.mkdir(parents=True, exist_ok=True)
    command = ["idt3tc", "img", str(source)]
    if size is not None:
        command.extend(["--resize", size])
    command.extend(["--save", str(output)])
    subprocess.run(command, check=True)


def _convert_model(source, output):
    # Like textures, generated models stay outside gamedir.
    output.parent.mkdir(parents=True, exist_ok=True)
    # Factory settings keep user add-ons and preferences out of the export,
    # and the exit code flag turns Python errors into a failed task.
    subprocess.run(
        [
            "blender",
            "--background",
            "--factory-startup",
            str(source),
            "--python-exit-code",
            "1",
            "--python",
            str(MD3_EXPORTER),
            "--",
            str(output),
        ],
        check=True,
    )


def _package_gamedir(output):
    """Combine gamedir and generated assets in a reproducible PK3."""
    gamedir = ROOT / "gamedir"
    sources = {
        source.relative_to(gamedir).as_posix(): source for source in gamedir.rglob("*")
    }
    # Replace existing asset entries rather than writing duplicate ZIP names.
    sources.update({name: ASSET_BUILD / name for name in GENERATED_ASSETS})
    with zipfile.ZipFile(output, "w") as archive:
        # Stable ordering and fixed metadata keep filesystem timestamps and
        # permissions from changing the archive bytes.
        for name, source in sorted(sources.items()):
            is_directory = source.is_dir()
            entry = zipfile.ZipInfo(
                name + "/" if is_directory else name,
                date_time=(1980, 1, 1, 0, 0, 0),
            )
            entry.create_system = 3
            mode = (stat.S_IFDIR | 0o755) if is_directory else (stat.S_IFREG | 0o644)
            entry.external_attr = (mode << 16) | (0x10 if is_directory else 0)
            archive.writestr(
                entry,
                b"" if is_directory else source.read_bytes(),
                compress_type=zipfile.ZIP_DEFLATED,
                compresslevel=9,
            )


def _assemble_dist():
    # Stage a fresh tree so patch failures preserve the previous distribution
    # and successful builds discard assets removed from gamedir.
    with tempfile.TemporaryDirectory(prefix=".dist-", dir=ROOT) as directory:
        staging = Path(directory) / "dist"
        staging.mkdir()
        _package_gamedir(staging / "igwlegacy.pk3")
        subprocess.run(
            [str(PATCHER), "--output-dir", str(staging), *map(str, INPUT_BINARIES)],
            check=True,
        )
        if DIST.exists():
            shutil.rmtree(DIST)
        staging.replace(DIST)


def task_monkey_patcher():
    """Compile monkey-patcher using Meson's incremental build."""
    return {
        "actions": [_build_patcher],
        "targets": [str(PATCHER)],
        "uptodate": [False],
    }


def task_textures():
    """Convert raw PNG assets into the DDS or TGA textures used by the game."""
    for name, (filename, size) in TEXTURE_RULES.items():
        source = ROOT / "raw-assets" / filename
        output = ASSET_BUILD / name
        yield {
            "name": Path(name).name,
            "actions": [(_convert_texture, [source, size, output])],
            "file_dep": [str(source)],
            "targets": [str(output)],
            "uptodate": [config_changed({"resize": size})],
        }


def task_models():
    """Export Blender models from raw-assets into the MD3s used by the game."""
    # The exporter imports these add-on modules, so changes to any of them
    # must re-export.
    addon_sources = sorted(str(path) for path in MD3_ADDON.rglob("*.py"))
    for name, filename in MODEL_RULES.items():
        source = ROOT / "raw-assets" / filename
        output = ASSET_BUILD / name
        yield {
            "name": Path(name).name,
            "actions": [(_convert_model, [source, output])],
            "file_dep": [str(source), str(MD3_EXPORTER), *addon_sources],
            "targets": [str(output)],
        }


def task_dist():
    """Package gamedir as igwlegacy.pk3 and add patched binaries in dist/."""
    return {
        "actions": [_assemble_dist],
        "task_dep": ["monkey_patcher", "textures", "models"],
        "file_dep": list(map(str, INPUT_BINARIES))
        + [str(ASSET_BUILD / name) for name in GENERATED_ASSETS],
        "targets": [str(DIST / "igwlegacy.pk3")]
        + [str(DIST / path.name) for path in INPUT_BINARIES],
        "uptodate": [False],
    }
