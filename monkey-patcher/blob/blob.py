#!/usr/bin/env python3
"""Build one module's C blob and emit it as C source for the patcher.

The sources are compiled freestanding for i686 with zig, linked at address
0 into a position independent ELF, and flattened into one image that the
patcher appends to the module's payload section. The blob relocates itself
at runtime (runtime.c), so only R_386_RELATIVE relocations are accepted
here; anything else would need the dynamic linker it lacks.

Functions defined with BLOB_EXPORT (default visibility) are the exports.
<module>_blob.h defines the offset of each as <MODULE>_BLOB_<NAME> and
declares <module>_blob, the blob_t that <module>_blob.c defines.
"""

import argparse
import re
import struct
import subprocess
import sys
from pathlib import Path

# Freestanding, x87 only, and no assumption beyond MSVC's 4-byte stack
# alignment, as blobs are called from and call into MSVC code.
CFLAGS = [
    "-target", "x86-freestanding-none", "-march=i686", "-std=c17",
    "-Os", "-fPIE", "-fvisibility=hidden", "-ffreestanding", "-fno-builtin",
    "-fno-stack-protector", "-fno-sanitize=all", "-fno-unwind-tables",
    "-fno-asynchronous-unwind-tables", "-ffunction-sections",
    "-fdata-sections", "-mno-sse", "-mno-mmx", "-mstack-alignment=4",
    "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter",
]

# What the patcher itself needs from every blob.
MAGIC = b"IGWBLOB1"
INIT = "blob_init"

R_386_RELATIVE = 8
PT_LOAD = 1
SHT_SYMTAB = 2
SHT_REL = 9
STB_GLOBAL = 1
STV_DEFAULT = 0
SHN_UNDEF = 0


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


def symbols(elf):
    """Yield (name, value, binding, visibility, section index) of every
    symbol in the symbol tables.
    """
    found = list(sections(elf))
    for _, kind, offset, size, link, entsize in found:
        if kind != SHT_SYMTAB:
            continue
        strings = found[link][2]
        for at in range(offset, offset + size, entsize):
            name, value, _, info, other, shndx = struct.unpack_from(
                "<IIIBBH", elf, at)
            end = elf.index(b"\0", strings + name)
            yield (elf[strings + name:end].decode(), value, info >> 4,
                   other & 3, shndx)


def exports(objects):
    """The functions the objects define with BLOB_EXPORT, sorted."""
    names = set()
    for obj in objects:
        for name, _, binding, visibility, shndx in symbols(obj.read_bytes()):
            if binding == STB_GLOBAL and visibility == STV_DEFAULT and shndx != SHN_UNDEF:
                names.add(name)
    return sorted(names)


def flatten(elf):
    """The file-backed bytes of the single load segment, and the size of
    the zeroed bss that follows them.
    """
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
    return elf[offset:offset + filesz], memsz - filesz


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


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--module", required=True,
                        help="the C name prefix, e.g. game for gamex86.dll")
    parser.add_argument("--zig", required=True)
    parser.add_argument("--include", action="append", default=[])
    parser.add_argument("--linker-script", required=True)
    parser.add_argument("--build-dir", required=True, type=Path)
    parser.add_argument("--source-output", required=True, type=Path)
    parser.add_argument("--header-output", required=True, type=Path)
    parser.add_argument("sources", nargs="+")
    args = parser.parse_args()
    if not re.fullmatch(r"[a-z][a-z0-9_]*", args.module):
        sys.exit(f"blob.py: bad module name {args.module}")

    args.build_dir.mkdir(parents=True, exist_ok=True)
    objects = []
    for source in args.sources:
        # Sources of different features may share a file name.
        obj = args.build_dir / f"{len(objects)}-{Path(source).stem}.o"
        run([args.zig, "cc", *CFLAGS, *(f"-I{d}" for d in args.include),
             "-c", source, "-o", obj])
        objects.append(obj)
    names = exports(objects)
    if not names:
        sys.exit("blob.py: no BLOB_EXPORT function")
    elf_path = args.build_dir / f"{args.module}_blob.elf"
    # -z notext allows the relocations in the writable, executable image;
    # --gc-sections drops whatever the exports and blob_init do not reach.
    run([args.zig, "ld.lld", "-m", "elf_i386", "-pie", "--no-dynamic-linker",
         "-z", "notext", "--gc-sections", "--entry=0", "-T", args.linker_script,
         *(f"--undefined={name}" for name in [INIT, *names]),
         "-o", elf_path, *objects])

    elf = elf_path.read_bytes()
    count = check_relocations(elf)
    image, bss = flatten(elf)
    table = {name: value for name, value, _, _, _ in symbols(elf)}
    missing = [name for name in [INIT, *names] if name not in table]
    if missing:
        sys.exit(f"blob.py: missing symbols {', '.join(missing)}")
    if table.get("blob_header") != 0 or image[:8] != MAGIC:
        sys.exit("blob.py: the blob header must come first")

    blob = f"{args.module}_blob"
    prefix = f"{args.module.upper()}_BLOB_"
    header = [
        f"/* Generated by monkey-patcher/blob/blob.py; do not edit. */",
        "#pragma once",
        "",
        '#include "patcher.h"',
        "",
        f"/* {len(image)} bytes, {bss} more of bss, {count} self-applied relocations. */",
        f"extern const blob_t {blob};",
        "",
        "/* The offsets of its exports, for emit_blob_call(). */",
    ]
    header += [f"#define {prefix}{name.upper()} 0x{table[name]:x}u" for name in names]
    header.append("")
    args.header_output.write_text("\n".join(header))

    source = [
        f"/* Generated by monkey-patcher/blob/blob.py; do not edit. */",
        f'#include "{args.header_output.name}"',
        "",
        "static const unsigned char data[] = {",
    ]
    for at in range(0, len(image), 16):
        source.append("    " + " ".join(f"0x{b:02x}," for b in image[at:at + 16]))
    source += [
        "};",
        "",
        f"const blob_t {blob} = {{ data, sizeof(data), {bss}u, 0x{table[INIT]:x}u }};",
        "",
    ]
    args.source_output.write_text("\n".join(source))


if __name__ == "__main__":
    main()
