"""Build the native patcher and assemble the game distribution."""

from pathlib import Path
import shutil
import stat
import subprocess
import tempfile
import zipfile


ROOT = Path(__file__).resolve().parent
PATCHER_SOURCE = ROOT / "monkey-patcher"
PATCHER_BUILD = PATCHER_SOURCE / "build"
PATCHER = PATCHER_BUILD / "igw-monkey-patcher"
INPUT_DLLS = [ROOT / "input-dlls" / name for name in ("gamex86.dll", "cgamex86.dll", "uix86.dll")]
DIST = ROOT / "dist"

DOIT_CONFIG = {"default_tasks": ["dist"]}


def _build_patcher():
    # Meson and Ninja track source changes and avoid unnecessary compilation.
    if not (PATCHER_BUILD / "meson-private" / "coredata.dat").is_file():
        subprocess.run(["meson", "setup", str(PATCHER_BUILD), str(PATCHER_SOURCE)], check=True)
    subprocess.run(["meson", "compile", "-C", str(PATCHER_BUILD)], check=True)


def _package_gamedir(output):
    """Write a reproducible PK3 with paths relative to gamedir/."""
    gamedir = ROOT / "gamedir"
    with zipfile.ZipFile(output, "w") as archive:
        # Stable ordering and fixed metadata keep filesystem timestamps and
        # permissions from changing the archive bytes.
        for source in sorted(gamedir.rglob("*")):
            is_directory = source.is_dir()
            name = source.relative_to(gamedir).as_posix()
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
            [str(PATCHER), "--output-dir", str(staging), *map(str, INPUT_DLLS)],
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


def task_dist():
    """Package gamedir as igwlegacy.pk3 and add patched DLLs in dist/."""
    return {
        "actions": [_assemble_dist],
        "task_dep": ["monkey_patcher"],
        "file_dep": list(map(str, INPUT_DLLS)),
        "targets": [str(DIST / "igwlegacy.pk3")] + [str(DIST / path.name) for path in INPUT_DLLS],
        "uptodate": [False],
    }
