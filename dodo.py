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
# igwarlord-server.exe is the dedicated server's igwarlord.exe, renamed to
# share the directory with the client's.
INPUT_BINARIES = [
    ROOT / "input-binaries" / name
    for name in (
        "gamex86.dll",
        "cgamex86.dll",
        "uix86.dll",
        "igwarlord.exe",
        "igwarlord-server.exe",
    )
]
DIST = ROOT / "dist"
# Name of the patched igwarlord.exe in dist/.
DIST_EXE_NAME = "igwlegacy.exe"
# The dedicated server keeps its name in the server directory of an IGW
# installation.
DIST_SERVER_EXE = Path("server", "igwarlord.exe")
# The dedicated server loads the server-side game logic from its own base
# directory, so it gets a copy of the patched gamex86.dll as well.
DIST_SERVER_GAME_DLL = Path("server", "base", "gamex86.dll")
# The game loads PK3s from its base game directory, like the DLLs.
DIST_PK3 = DIST / "base" / "zzzigwlegacy.pk3"
ASSET_BUILD = ROOT / "build" / "assets"
# Use None for the size to convert formats while preserving source dimensions.
TEXTURE_RULES = {
    "textures/gfx/cursor.dds": ("cursor.png", None),
    "textures/ui/cursor.dds": ("cursor.png", None),

    "textures/ui/mmbg1.dds": ("wallpaper.png", "2048x2048"),
    "textures/ui/mmbg1ws.dds": ("wallpaper.png", "2048x2048"),
    "textures/ui/mmbottomedge1.dds": ("nothing.png", "256x256"),
    "textures/ui/mmcorner.dds": ("nothing.png", "256x256"),
    "textures/ui/mmrightborder.dds": ("nothing.png", "256x256"),
    "textures/ui/mmsideedge1.dds": ("nothing.png", "256x256"),
    "textures/ui/mmsideedge2.dds": ("nothing.png", "256x256"),
    # "textures/ui/mmtopedge1.dds": ("nothing.png", "256x256"),
    "textures/ui/mmtopleftpart.dds": ("nothing.png", "256x256"),
    "textures/ui/mmtoplogo.dds": ("mmlogo.png", "512x512"),
    "textures/ui/mmtoplogoSE.dds": ("mmlogo.png", "512x512"),
    # "textures/ui/button.dds": ("buttons.png", "512x512"),
    # "textures/ui/button/button-1.dds": ("button1.png", "512x128"),
    # "textures/ui/button/button-2.dds": ("button2.png", "512x128"),
    # "textures/ui/button/button-3.dds": ("button3.png", "512x128"),
    # "textures/ui/button/button-4.dds": ("button4.png", "512x128"),
    "textures/ui/loading/progressbar-green.dds": ("blood.png", None),
    # The shader references the .tga, so ship it alongside the .dds.
    # "models/mapobjects/item_highlight/item_highlight.tga": (
    #     "models/item_highlight.png",
    #     None,
    # ),
    "models/mapobjects/item_highlight/item_highlight.dds": (
        "models/item_highlight.png",
        None,
    ),
    "models/mapobjects/item_highlight/item_highlight_power.dds": (
        "highlight_power.png",
        "512x512",
    ),
    "models/mapobjects/item_highlight/item_highlight_stronghold.dds": (
        "highlight_stronghold.png",
        "512x512",
    ),
    "models/mapobjects/item_highlight/item_highlight_dynamite.dds": (
        "highlight_dynamite.png",
        "512x512",
    ),
    "models/mapobjects/item_highlight/item_highlight_rocket.dds": (
        "highlight_rocket.png",
        "512x512",
    ),
    "models/mapobjects/item_highlight/item_highlight_radio.dds": (
        "highlight_radio.png",
        "512x512",
    ),
}

# Blender sources below raw-assets/ exported as static MD3s, with the shader
# name written to every surface. One .blend can thus back several models,
# e.g. the round and square highlights with different textures. The shaders
# themselves live in gamedir/scripts/.
MODEL_RULES = {
    "models/mapobjects/item_highlight/item_highlight.md3": (
        "models/item_highlight_round.blend",
        "models/mapobjects/item_highlight/item_highlight",
    ),
    "models/mapobjects/item_highlight/item_highlight_power.md3": (
        "models/item_highlight_round.blend",
        "models/mapobjects/item_highlight/item_highlight_power",
    ),
    "models/mapobjects/item_highlight/item_highlight_stronghold.md3": (
        "models/item_highlight_square.blend",
        "models/mapobjects/item_highlight/item_highlight_stronghold",
    ),    "models/mapobjects/item_highlight/item_highlight_dynamite.md3": (
        "models/item_highlight_round.blend",
        "models/mapobjects/item_highlight/item_highlight_dynamite",
    ),
    "models/mapobjects/item_highlight/item_highlight_rocket.md3": (
        "models/item_highlight_round.blend",
        "models/mapobjects/item_highlight/item_highlight_rocket",
    ),
    "models/mapobjects/item_highlight/item_highlight_radio.md3": (
        "models/item_highlight_round.blend",
        "models/mapobjects/item_highlight/item_highlight_radio",
    ),
}
MD3_EXPORTER = ROOT / "model-converter" / "export_md3.py"
MD3_ADDON = ROOT / "model-converter" / "Blender_BSP_Importer" / "import_bsp"
GENERATED_ASSETS = [*TEXTURE_RULES, *MODEL_RULES]

DOIT_CONFIG = {"default_tasks": ["dist"]}


def _build_patcher():
    # Meson and Ninja track source changes and avoid unnecessary compilation.
    if not (PATCHER_BUILD / "meson-private" / "coredata.dat").is_file():
        subprocess.run(["meson", "setup", str(PATCHER_BUILD), str(ROOT)], check=True)
    # Call Ninja directly: "meson compile" only wraps it but costs a Python
    # interpreter start on every build. Ninja still regenerates the build
    # files itself when a meson.build changes.
    subprocess.run(["ninja", "-C", str(PATCHER_BUILD)], check=True)


def _convert_texture(source, size, output):
    # Generated textures stay outside gamedir, which remains a source tree.
    output.parent.mkdir(parents=True, exist_ok=True)
    command = ["idt3tc", "img", str(source)]
    if size is not None:
        command.extend(["--resize", size])
    command.extend(["--save", str(output)])
    subprocess.run(command, check=True)


def _convert_model(source, shader, output):
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
            shader,
        ],
        check=True,
    )


def _gamedir_sources():
    """Map PK3 entry names to their files and directories below gamedir."""
    gamedir = ROOT / "gamedir"
    return {
        source.relative_to(gamedir).as_posix(): source for source in gamedir.rglob("*")
    }


def _package_gamedir(output):
    """Combine gamedir and generated assets in a reproducible PK3."""
    sources = _gamedir_sources()
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


def _dist_path(binary):
    """Return where a patched binary goes below dist/."""
    # The DLLs belong in the base game directory, the executable stays at
    # the top of the distribution under the IGWL name so it is not mistaken
    # for the unpatched original.
    if binary.suffix.lower() == ".dll":
        return Path("base", binary.name)
    if binary.name == "igwarlord-server.exe":
        return DIST_SERVER_EXE
    return Path(DIST_EXE_NAME)


def _assemble_dist():
    # Stage a fresh tree so patch failures preserve the previous distribution
    # and successful builds discard assets removed from gamedir.
    with tempfile.TemporaryDirectory(prefix=".dist-", dir=ROOT) as directory:
        staging = Path(directory) / "dist"
        staging.mkdir()
        pk3 = staging / DIST_PK3.relative_to(DIST)
        pk3.parent.mkdir(parents=True)
        _package_gamedir(pk3)
        subprocess.run(
            [str(PATCHER), "--output-dir", str(staging), *map(str, INPUT_BINARIES)],
            check=True,
        )
        # The patcher writes every binary to one directory; sort them into
        # their final places while still staging.
        for binary in INPUT_BINARIES:
            target = staging / _dist_path(binary)
            target.parent.mkdir(parents=True, exist_ok=True)
            (staging / binary.name).replace(target)
        server_game_dll = staging / DIST_SERVER_GAME_DLL
        server_game_dll.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(staging / "base" / "gamex86.dll", server_game_dll)
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
            # Keep the directory so matching filenames have distinct tasks.
            "name": name,
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
    for name, (filename, shader) in MODEL_RULES.items():
        source = ROOT / "raw-assets" / filename
        output = ASSET_BUILD / name
        yield {
            "name": Path(name).name,
            "actions": [(_convert_model, [source, shader, output])],
            "file_dep": [str(source), str(MD3_EXPORTER), *addon_sources],
            "targets": [str(output)],
            # Re-export when only the shader of a rule changes.
            "uptodate": [config_changed({"shader": shader})],
        }


def task_dist():
    """Package gamedir as igwlegacy.pk3 and add patched binaries in dist/.

    The PK3 and the patched DLLs go to dist/base/, the client executable
    to dist/igwlegacy.exe and the dedicated server's to
    dist/server/igwarlord.exe, next to a copy of the patched gamex86.dll
    in dist/server/base/.
    """
    gamedir = _gamedir_sources()
    return {
        "actions": [_assemble_dist],
        "task_dep": ["monkey_patcher", "textures", "models"],
        # Every input of the distribution is a dependency, so a build without
        # changes skips repackaging. dodo.py covers the packaging code and
        # rules, the patcher binary covers changes to the patches.
        "file_dep": [str(ROOT / "dodo.py"), str(PATCHER)]
        + list(map(str, INPUT_BINARIES))
        + [str(ASSET_BUILD / name) for name in GENERATED_ASSETS]
        + [str(path) for path in gamedir.values() if path.is_file()],
        "targets": [str(DIST_PK3), str(DIST / DIST_SERVER_GAME_DLL)]
        + [str(DIST / _dist_path(path)) for path in INPUT_BINARIES],
        # File dependencies alone miss removed files and empty directories,
        # which also change the PK3, so track the entry names as well.
        "uptodate": [config_changed({"gamedir": sorted(gamedir)})],
    }
