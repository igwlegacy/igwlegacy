#!/usr/bin/env python3
"""Build the TOML wave blob and emit it as a C header for the patcher.

The sources are compiled freestanding for i686 with zig, linked at address
0 into a position independent ELF, and flattened into one image that the
patcher copies into gamex86.dll's payload section. The blob relocates
itself at runtime (runtime.c), so only R_386_RELATIVE relocations are
accepted here; anything else would need the dynamic linker it lacks.
"""

import argparse
import struct
import subprocess
import sys
from pathlib import Path

# Freestanding, x87 only, and no assumption beyond MSVC's 4-byte stack
# alignment, as the blob is called from and calls into MSVC code.
CFLAGS = [
    "-target", "x86-freestanding-none", "-march=i686", "-std=c17",
    "-Os", "-fPIE", "-fvisibility=hidden", "-ffreestanding", "-fno-builtin",
    "-fno-stack-protector", "-fno-sanitize=all", "-fno-unwind-tables",
    "-fno-asynchronous-unwind-tables", "-ffunction-sections",
    "-fdata-sections", "-mno-sse", "-mno-mmx", "-mstack-alignment=4",
    "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter",
]

# The symbols the payload calls or patches, by their names in the header.
EXPORTS = {
    "blob_header": "TOML_WAVES_HEADER",
    "toml_waves_load": "TOML_WAVES_LOAD",
    "toml_waves_run": "TOML_WAVES_RUN",
}

R_386_RELATIVE = 8
PT_LOAD = 1
SHT_SYMTAB = 2
SHT_REL = 9


def run(command):
    result = subprocess.run(command)
    if result.returncode:
        sys.exit(f"blob.py: {' '.join(map(str, command))} failed")


def sections(elf):
    """Yield (name, type, offset, size, link, entsize) of every section."""
    shoff, = struct.unpack_from("<I", elf, 0x20)
    shentsize, shnum, shstrndx = struct.unpack_from("<HHH", elf, 0x2e)
    headers = [struct.unpack_from("<IIIIIIIIII", elf, shoff + i * shentsize)
               for i in range(shnum)]
    names = headers[shstrndx][4]
    for h in headers:
        end = elf.index(b"\0", names + h[0])
        yield elf[names + h[0]:end].decode(), h[1], h[4], h[5], h[6], h[9]


def flatten(elf):
    """The image of the single load segment, its bss included."""
    if elf[:4] != b"\x7fELF" or elf[4] != 1 or elf[5] != 1:
        sys.exit("blob.py: not a 32-bit little-endian ELF")
    phoff, = struct.unpack_from("<I", elf, 0x1c)
    phentsize, phnum = struct.unpack_from("<HH", elf, 0x2a)
    loads = []
    for i in range(phnum):
        p_type, offset, vaddr, _, filesz, memsz, _, _ = struct.unpack_from(
            "<IIIIIIII", elf, phoff + i * phentsize)
        if p_type == PT_LOAD:
            loads.append((offset, vaddr, filesz, memsz))
    if len(loads) != 1 or loads[0][1] != 0:
        sys.exit("blob.py: expected one load segment at address 0")
    offset, _, filesz, memsz = loads[0]
    return elf[offset:offset + filesz] + bytes(memsz - filesz)


def check_relocations(elf):
    count = 0
    for name, kind, offset, size, _, _ in sections(elf):
        if kind != SHT_REL:
            continue
        for at in range(offset, offset + size, 8):
            _, info = struct.unpack_from("<II", elf, at)
            if info & 0xff != R_386_RELATIVE:
                sys.exit(f"blob.py: {name} has relocation type {info & 0xff}")
            count += 1
    return count


def symbols(elf):
    table = {}
    found = list(sections(elf))
    for _, kind, offset, size, link, entsize in found:
        if kind != SHT_SYMTAB:
            continue
        strings = found[link][2]
        for at in range(offset, offset + size, entsize):
            name, value = struct.unpack_from("<II", elf, at)
            end = elf.index(b"\0", strings + name)
            table[elf[strings + name:end].decode()] = value
    return table


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--zig", required=True)
    parser.add_argument("--include", action="append", default=[])
    parser.add_argument("--linker-script", required=True)
    parser.add_argument("--build-dir", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("sources", nargs="+")
    args = parser.parse_args()

    args.build_dir.mkdir(parents=True, exist_ok=True)
    objects = []
    for source in args.sources:
        obj = args.build_dir / (Path(source).stem + ".o")
        run([args.zig, "cc", *CFLAGS, *(f"-I{d}" for d in args.include),
             "-c", source, "-o", obj])
        objects.append(obj)
    elf_path = args.build_dir / "toml_waves.elf"
    # -z notext allows the relocations in the writable, executable image;
    # --gc-sections drops the unused parts of tomlc17 and the C library,
    # and keeps what the exports reach.
    run([args.zig, "ld.lld", "-m", "elf_i386", "-pie", "--no-dynamic-linker",
         "-z", "notext", "--gc-sections", "--entry=0", "-T", args.linker_script,
         *(f"--undefined={name}" for name in EXPORTS), "-o", elf_path, *objects])

    elf = elf_path.read_bytes()
    count = check_relocations(elf)
    image = flatten(elf)
    table = symbols(elf)
    missing = [name for name in EXPORTS if name not in table]
    if missing:
        sys.exit(f"blob.py: missing symbols {', '.join(missing)}")
    if table["blob_header"] != 0 or image[:8] != b"IGWTOML1":
        sys.exit("blob.py: the blob header must come first")

    lines = [
        "/* Generated by patches/toml_waves/blob.py; do not edit. */",
        "#pragma once",
        "",
        f"/* {len(image)} bytes, {count} self-applied relocations. */",
    ]
    lines += [f"#define {macro} 0x{table[name]:x}u" for name, macro in EXPORTS.items()]
    lines += [
        "/* The RVA field follows the 8-byte magic. */",
        "#define TOML_WAVES_HEADER_RVA (TOML_WAVES_HEADER + 8u)",
        "",
        "static const unsigned char toml_waves_blob[] = {",
    ]
    for at in range(0, len(image), 16):
        lines.append("    " + " ".join(f"0x{b:02x}," for b in image[at:at + 16]))
    lines += ["};", ""]
    args.output.write_text("\n".join(lines))


if __name__ == "__main__":
    main()
