#!/usr/bin/env python3
"""Pack the initial, code-only ELF32/m68k profile into an S9M1 image."""

import argparse
import re
import struct
import sys
import zlib
from pathlib import Path

# Bounds the loader enforces (ssos/os/module/image.c). The default leaves a
# C module room for its own call depth above the kernel syscall paths.
STACK_MIN = 4096
STACK_MAX = 262144
STACK_DEFAULT = 32768
# Stack a module must keep free for the kernel: syscall entry and the
# storage paths run on the caller's stack (about 1 KiB; IOCS included here).
KERNEL_STACK_RESERVE = 4096


def stack_budget(usage_files):
    """Largest frames from -fstack-usage files: the entry function's and the
    largest of any other function. A module keeps all its state in main(), so
    main alone must leave room for one nested call and the kernel."""
    entry = other = 0
    for path in usage_files:
        for line in Path(path).read_text().splitlines():
            head, size, _ = line.split("\t")
            frame = int(size)
            if head.rsplit(":", 1)[-1].split(".")[0] == "main":
                entry = max(entry, frame)
            else:
                other = max(other, frame)
    return entry, other


def pack(raw, name, stack_size=STACK_DEFAULT, stack_usage=None):
    def check(condition, message):
        if not condition:
            raise ValueError(message)

    def unpack(fmt, offset):
        size = struct.calcsize(fmt)
        check(0 <= offset <= len(raw) - size, "truncated ELF")
        return struct.unpack_from(fmt, raw, offset)

    check(re.fullmatch(r"[A-Za-z0-9_-][A-Za-z0-9_.-]{0,30}", name)
          and not name.endswith("."), "invalid module name")
    check(STACK_MIN <= stack_size <= STACK_MAX and stack_size % 4 == 0,
          "invalid module stack size")
    if stack_usage:
        entry, other = stack_budget(stack_usage)
        check(entry + other + KERNEL_STACK_RESERVE <= stack_size,
              f"stack budget exceeded: main frame {entry} + largest nested "
              f"frame {other} + kernel reserve {KERNEL_STACK_RESERVE} > "
              f"{stack_size}")
    header = unpack(">16sHHIIIIIHHHHHH", 0)
    ident, kind, machine, version, entry, _, shoff, flags, ehsize, _, _, shsize, shnum, shstr = header
    check(ident[:7] == b"\x7fELF\x01\x02\x01" and kind == 2 and machine == 4
          and version == 1 and flags == 0x01000000 and ehsize == 52
          and shsize == 40 and 0 < shnum <= 256 and shstr < shnum,
          "unsupported ELF header")
    sections = [unpack(">IIIIIIIIII", shoff + i * shsize) for i in range(shnum)]

    def contents(section):
        offset, size = section[4:6]
        check(offset <= len(raw) and size <= len(raw) - offset, "invalid section range")
        return raw[offset:offset + size]

    names = contents(sections[shstr])

    def section_name(section):
        offset = section[0]
        check(offset < len(names), "invalid section name")
        end = names.find(b"\0", offset)
        check(end >= 0, "unterminated section name")
        return names[offset:end]

    text_indices = [i for i, s in enumerate(sections) if section_name(s) == b".text"]
    check(len(text_indices) == 1, "missing or duplicate .text")
    text_index = text_indices[0]
    text = sections[text_index]
    check(text[1] == 1 and text[2] == 6 and text[3] == 0
          and 0 < text[5] <= 32768 and entry < text[5] and entry % 2 == 0,
          "unsupported code section or entry")
    code = contents(text)
    relocation_sections = 0
    for i, sec in enumerate(sections):
        if i != text_index and sec[2] & 2:
            check(sec[5] == 0, "allocated data is not supported")
        if sec[1] == 2:
            check(sec[9] == 16 and sec[5] % 16 == 0, "invalid symbol table")
            table = contents(sec)
            for pos in range(16, len(table), 16):
                _, _, _, info, _, index = struct.unpack_from(">IIIBBH", table, pos)
                check(index != 0 or info >> 4 == 0, "undefined symbol")
        if sec[1] == 9:
            raise ValueError("REL relocation is not supported")
        if sec[1] != 4:
            continue
        relocation_sections += 1
        check(sec[7] == text_index and sec[6] < shnum and sec[9] == 12
              and sec[5] > 0 and sec[5] % 12 == 0 and relocation_sections == 1,
              "unsupported relocation section")
        symbols = sections[sec[6]]
        check(symbols[1] == 2 and symbols[9] == 16 and symbols[5] % 16 == 0,
              "invalid symbol table")
        symdata = contents(symbols)
        records = contents(sec)
        seen = set()
        for offset in range(0, len(records), 12):
            address, info, addend = struct.unpack_from(">IIi", records, offset)
            symbol_index, relocation = info >> 8, info & 255
            check(relocation == 5 and symbol_index < len(symdata) // 16
                  and address % 2 == 0 and address + 2 <= len(code)
                  and address not in seen, "unsupported relocation")
            seen.add(address)
            _, value, _, _, _, index = struct.unpack_from(">IIIBBH", symdata, symbol_index * 16)
            displacement = value + addend - address
            check(index == text_index and 0 <= value + addend < len(code)
                  and -32768 <= displacement <= 32767
                  and struct.unpack_from(">h", code, address)[0] == displacement,
                  "invalid PC-relative relocation")

    check(relocation_sections == 1, "missing code relocation section")

    encoded_name = name.encode("ascii")
    code_offset = 80
    data_offset = (code_offset + len(code) + 3) & ~3
    name_offset = data_offset + 4
    image = bytearray(name_offset + len(encoded_name))
    image[:4] = b"S9M1"
    struct.pack_into(">HHIIIIIIIII", image, 4, 1, 80, 3, len(image), code_offset,
                     len(code), code_offset + entry, data_offset, 4, 0, stack_size)
    struct.pack_into(">IH", image, 0x2c, name_offset, len(encoded_name))
    struct.pack_into(">IIII", image, 0x38, 0, 4, 0, 0)
    image[code_offset:code_offset + len(code)] = code
    image[name_offset:] = encoded_name
    struct.pack_into(">I", image, 0x34, zlib.crc32(image))
    return image


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("elf", type=Path)
    parser.add_argument("--name", required=True)
    parser.add_argument("--stack-size", type=int, default=STACK_DEFAULT)
    parser.add_argument("--stack-usage", type=Path, nargs="*", default=[],
                        help="-fstack-usage files (.su) of the module objects; "
                             "fails when main plus one nested frame and the "
                             "kernel reserve do not fit the stack")
    parser.add_argument("-o", "--output", required=True, type=Path)
    args = parser.parse_args()
    try:
        image = pack(args.elf.read_bytes(), args.name, args.stack_size,
                     args.stack_usage)
        args.output.write_bytes(image)
    except (OSError, ValueError, struct.error) as error:
        print(f"mkmodule: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
