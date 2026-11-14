"""Build the native patcher and assemble the game distribution."""

from pathlib import Path
import shutil
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parent
PATCHER_SOURCE = ROOT / "monkey-patcher"
PATCHER_BUILD = PATCHER_SOURCE / "build"
PATCHER = PATCHER_BUILD / "igw-monkey-patcher"
INPUT_DLLS = [ROOT / "input-dlls" / name for name in ("gamex86.dll", "cgamex86.dll")]
DIST = ROOT / "dist"

DOIT_CONFIG = {"default_tasks": ["dist"]}


def _build_patcher():
    # Meson and Ninja track source changes and avoid unnecessary compilation.
    if not (PATCHER_BUILD / "meson-private" / "coredata.dat").is_file():
        subprocess.run(["meson", "setup", str(PATCHER_BUILD), str(PATCHER_SOURCE)], check=True)
    subprocess.run(["meson", "compile", "-C", str(PATCHER_BUILD)], check=True)


def _assemble_dist():
    # Stage a fresh tree so patch failures preserve the previous distribution
    # and successful builds discard assets removed from gamedir.
    with tempfile.TemporaryDirectory(prefix=".dist-", dir=ROOT) as directory:
        staging = Path(directory) / "dist"
        shutil.copytree(ROOT / "gamedir", staging, symlinks=True)
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
    """Copy gamedir verbatim and add patched DLLs at the root of dist/."""
    return {
        "actions": [_assemble_dist],
        "task_dep": ["monkey_patcher"],
        "file_dep": list(map(str, INPUT_DLLS)),
        "targets": [str(DIST / path.name) for path in INPUT_DLLS],
        "uptodate": [False],
    }
