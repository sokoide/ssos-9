#!/usr/bin/env python3
"""Validate and dump an S9O1 assembler object file."""

import argparse
import struct
import sys
from pathlib import Path

SECTIONS = {0: "abs", 1: "text", 2: "data", 3: "bss", 0xFF: "undef"}
RELOC_TYPES = {1: "PC16", 2: "ABS32", 3: "ABS16", 4: "ABS8"}

HEADER = struct.Struct(">4sHHIIIIIIII")
SYMBOL = struct.Struct(">IBBHI")
RELOC = struct.Struct(">IIIBBH")


def check(condition, message):
    if not condition:
        raise ValueError(message)


def signed32(value):
    return value - 0x100000000 if value >= 0x80000000 else value


def pool_name(pool, name_offset):
    check(0 <= name_offset < len(pool), f"name offset {name_offset} outside name pool")
    end = pool.find(b"\0", name_offset)
    check(end >= 0, f"unterminated name at pool offset {name_offset}")
    return pool[name_offset:end]


def parse(raw):
    check(len(raw) >= HEADER.size, "truncated header")
    magic, version, flags, text_size, data_size, bss_size, entry, symbol_count, \
        reloc_count, pool_size, reserved = HEADER.unpack_from(raw, 0)
    check(magic == b"S9O1", "bad magic")
    check(version == 1, f"unsupported version {version}")
    check(flags == 0, f"unsupported flags {flags:#x}")
    check(entry == 0, f"entry must be 0, is {entry:#x}")
    check(reserved == 0, f"reserved must be 0, is {reserved:#x}")
    expected = HEADER.size + text_size + data_size + symbol_count * SYMBOL.size \
        + reloc_count * RELOC.size + pool_size
    check(len(raw) == expected,
          f"file length {len(raw)} does not match header fields ({expected})")

    at = HEADER.size
    text = raw[at:at + text_size]
    at += text_size
    data = raw[at:at + data_size]
    at += data_size

    symbols = []
    for index in range(symbol_count):
        name_offset, section, sym_flags, sym_reserved, value = \
            SYMBOL.unpack_from(raw, at + index * SYMBOL.size)
        check(section in SECTIONS, f"symbol {index}: invalid section {section:#x}")
        check(sym_flags & ~0x07 == 0, f"symbol {index}: invalid flags {sym_flags:#x}")
        check(sym_reserved == 0, f"symbol {index}: reserved must be 0")
        symbols.append((name_offset, section, sym_flags, value))
    at += symbol_count * SYMBOL.size

    relocs = []
    for index in range(reloc_count):
        offset, symbol, addend, section, reloc_type, reloc_reserved = \
            RELOC.unpack_from(raw, at + index * RELOC.size)
        check(section in (1, 2), f"reloc {index}: invalid section {section:#x}")
        check(reloc_type in RELOC_TYPES, f"reloc {index}: invalid type {reloc_type}")
        check(reloc_reserved == 0, f"reloc {index}: reserved must be 0")
        check(symbol < symbol_count, f"reloc {index}: symbol index {symbol} out of range")
        limit = text_size if section == 1 else data_size
        check(0 <= offset < limit, f"reloc {index}: offset {offset:#x} outside section")
        relocs.append((offset, symbol, signed32(addend), section, reloc_type))
    at += reloc_count * RELOC.size

    pool = raw[at:at + pool_size]
    names = [pool_name(pool, entry_offset) for entry_offset, _, _, _ in symbols]
    return {
        "version": version,
        "flags": flags,
        "text": text,
        "data": data,
        "bss_size": bss_size,
        "symbols": symbols,
        "names": names,
        "relocs": relocs,
    }


def flag_names(sym_flags):
    parts = []
    if sym_flags & 0x01:
        parts.append("global")
    if sym_flags & 0x02:
        parts.append("extern")
    if sym_flags & 0x04:
        parts.append("section")
    return "|".join(parts) if parts else "none"


def hexdump(label, blob):
    print(f"{label} ({len(blob)} bytes)")
    for off in range(0, len(blob), 16):
        chunk = blob[off:off + 16]
        print(f"  {off:04x}  {' '.join(f'{b:02x}' for b in chunk)}")
    if not blob:
        print("  (empty)")


def dump(obj):
    print(f"version {obj['version']} flags {obj['flags']:#x} bss {obj['bss_size']}")
    print(f"symbols {len(obj['symbols'])} relocs {len(obj['relocs'])}")
    for index, ((_, section, sym_flags, value), name) in \
            enumerate(zip(obj["symbols"], obj["names"])):
        print(f"symbol {index}: {name.decode('ascii', 'backslashreplace')} "
              f"section {SECTIONS[section]} flags {flag_names(sym_flags)} "
              f"value {value:#x}")
    for index, (offset, symbol, addend, section, reloc_type) in \
            enumerate(obj["relocs"]):
        name = obj["names"][symbol].decode("ascii", "backslashreplace")
        print(f"reloc {index}: section {SECTIONS[section]} type "
              f"{RELOC_TYPES[reloc_type]} offset {offset:#x} symbol {name} "
              f"addend {addend}")
    hexdump("text", obj["text"])
    hexdump("data", obj["data"])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("object", type=Path)
    parser.add_argument("--text-only", action="store_true",
                        help="print only the .text bytes as one hex string")
    args = parser.parse_args()
    try:
        obj = parse(args.object.read_bytes())
        if args.text_only:
            print(obj["text"].hex())
        else:
            dump(obj)
    except (OSError, ValueError, struct.error) as error:
        print(f"s9ob: {args.object}: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
