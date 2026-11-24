#!/usr/bin/env python3
"""Golden tests for the SSOS-9 linker (apps/link, Phase 4).

Two levels, both driven through the host build of the linker:

1. Cross-comparison against m68k-xelf-ld (tests/asm/link/golden/*/). Each
   directory holds S sources linked in sorted file order. The objects are
   built with asm-host (S9O1) and m68k-xelf-as (ELF), linked with link-host
   and m68k-xelf-ld respectively, and the linked text bytes are compared.
   Only the profile GNU ld can express is used here: text concatenation,
   PC16 folding, constant data, every object's text length even, no A5
   displacement and no data pointers (docs/linker-contract.md, 検証).

2. Self-check vectors (tests/asm/link/start.s, main.s, obj.s): the sources
   used by the QEMU t14 integration. They exercise A5 displacement folding,
   extern data/text resolution and a CODE32 fixup, which GNU ld has no
   counterpart for, so this script contains an independent mini-linker for
   exactly that profile and compares link-host's S9M1 output byte for byte
   (header fields, code, init data, fixups, CRC). Two link orders are
   checked: input order and a definition-first order (obj.s linked before
   main.s, which references its helper/factor), so resolution cannot
   depend on the definition preceding the reference. badmain.s must fail
   with an undefined-symbol diagnostic and must not produce an output
   file.

link-host is the host CLI driver built by `make -C tests test-link-host`
(tests/unit/link_host_main.c plus the apps/link core) and is invoked with
the same arguments as the on-target CLI: link-host OUTPUT IN1 [IN2 ...].
m68k-xelf tools need `source ~/.elf2x68k`.
"""

import shutil
import struct
import subprocess
import sys
import tempfile
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
LINK_CASES = ROOT / "tests" / "asm" / "link"

OBJ_HEADER = struct.Struct(">4sHHIIIIIIII")  # 40 bytes
OBJ_SYMBOL = struct.Struct(">IBBHI")         # 12 bytes
OBJ_RELOC = struct.Struct(">IIIBBH")         # 16 bytes

SECTION_ABS = 0
SECTION_TEXT = 1
SECTION_DATA = 2
SECTION_BSS = 3
SECTION_UNDEF = 0xFF
RELOC_PC16 = 1
RELOC_ABS32 = 2
RELOC_ABS16 = 3

FLAG_GLOBAL = 0x01
FLAG_EXTERN = 0x02

M1_HEADER_SIZE = 80
M1_FLAG_PIC = 0x00000001
M1_FLAG_REENTRANT = 0x00000002
M1_STACK_SIZE = 4096
M1_GOT_SIZE = 4


class LinkError(Exception):
    pass


def run(command):
    return subprocess.run([str(c) for c in command], capture_output=True,
                          text=True)


def be16(raw, at):
    return (raw[at] << 8) | raw[at + 1]


def be32(raw, at):
    return int.from_bytes(raw[at:at + 4], "big")


def patch(raw, at, value, width):
    raw[at:at + width] = (value & ((1 << (width * 8)) - 1)).to_bytes(
        width, "big")


def parse_s9o1(path):
    raw = path.read_bytes()
    if len(raw) < OBJ_HEADER.size:
        raise LinkError(f"{path.name}: truncated header")
    magic, version, flags, text_size, data_size, bss_size, entry, \
        symbol_count, reloc_count, pool_size, reserved = \
        OBJ_HEADER.unpack_from(raw, 0)
    if magic != b"S9O1" or version != 1 or flags != 0 or entry != 0 or \
            reserved != 0:
        raise LinkError(f"{path.name}: not a valid S9O1 object")
    expected = OBJ_HEADER.size + text_size + data_size + \
        symbol_count * OBJ_SYMBOL.size + reloc_count * OBJ_RELOC.size + \
        pool_size
    if len(raw) != expected:
        raise LinkError(f"{path.name}: length does not match header")
    at = OBJ_HEADER.size
    text = raw[at:at + text_size]
    at += text_size
    data = raw[at:at + data_size]
    at += data_size
    pool = raw[at + symbol_count * OBJ_SYMBOL.size +
               reloc_count * OBJ_RELOC.size:]
    symbols = []
    for index in range(symbol_count):
        name_offset, section, sym_flags, sym_reserved, value = \
            OBJ_SYMBOL.unpack_from(raw, at + index * OBJ_SYMBOL.size)
        end = pool.find(b"\0", name_offset)
        if end < 0:
            raise LinkError(f"{path.name}: unterminated symbol name")
        symbols.append((pool[name_offset:end].decode("latin-1"), section,
                        sym_flags, value))
    at += symbol_count * OBJ_SYMBOL.size
    relocs = []
    for index in range(reloc_count):
        relocs.append(OBJ_RELOC.unpack_from(raw, at + index * OBJ_RELOC.size))
    return {"text": text, "data": data, "bss_size": bss_size,
            "symbols": symbols, "relocs": relocs}


def mini_link(objs):
    """Independent linker for the t14 sample profile: text concatenation
    (odd previous text gets one 0x00 pad byte), data/bss concatenation into
    private data, PC16 (text->text) and ABS16 (text->data/bss/abs) folding,
    ABS32 (data->abs) folding and ABS32 (data->text/data/bss) fixups."""
    code = bytearray()
    text_bases = []
    for index, obj in enumerate(objs):
        if index > 0 and len(objs[index - 1]["text"]) % 2 != 0:
            code.append(0)
        text_bases.append(len(code))
        code += obj["text"]
    text_total = len(code)

    data_bases = []
    data = bytearray()
    for obj in objs:
        data_bases.append(len(data))
        data += obj["data"]
    data_total = len(data)

    bss_bases = []
    bss_offset = 0
    for obj in objs:
        bss_bases.append(bss_offset)
        bss_offset += obj["bss_size"]
    bss_total = bss_offset

    definitions = {}
    for index, obj in enumerate(objs):
        for name, section, flags, value in obj["symbols"]:
            if flags & FLAG_GLOBAL:
                if name in definitions:
                    raise LinkError(f"Duplicate symbol {name}")
                definitions[name] = (index, section, value)

    def resolve(obj_index, symbol_index):
        name, section, flags, value = \
            objs[obj_index]["symbols"][symbol_index]
        if flags & FLAG_EXTERN:
            target = definitions.get(name)
            if target is None:
                raise LinkError(f"Undefined symbol {name}")
            return target
        return (obj_index, section, value)

    def out_address(obj_index, section, value):
        if section == SECTION_TEXT:
            return text_bases[obj_index] + value
        if section == SECTION_DATA:
            return data_bases[obj_index] + value
        if section == SECTION_BSS:
            return data_total + bss_bases[obj_index] + value
        if section == SECTION_ABS:
            return value
        raise LinkError("undefined section")

    fixups = []
    fixup_targets = set()
    for index, obj in enumerate(objs):
        for offset, symbol_index, addend, place, kind, _reserved in \
                obj["relocs"]:
            if place not in (SECTION_TEXT, SECTION_DATA):
                raise LinkError("reloc outside text/data")
            target_index, target_section, target_value = \
                resolve(index, symbol_index)
            if target_section == SECTION_UNDEF:
                raise LinkError("unresolved reloc symbol")
            resolved = out_address(target_index, target_section,
                                   target_value)
            if place == SECTION_TEXT:
                field = text_bases[index] + offset
                if kind == RELOC_PC16:
                    if target_section != SECTION_TEXT:
                        raise LinkError("PIC violation: PC16 to non-text")
                    value = resolved + addend - field
                    if not -32768 <= value <= 32767:
                        raise LinkError("PC16 out of range")
                    patch(code, field, value, 2)
                elif kind == RELOC_ABS16:
                    if target_section in (SECTION_DATA, SECTION_BSS):
                        value = resolved + addend
                        if not -32768 <= value <= 32767:
                            raise LinkError("A5 displacement out of range")
                    elif target_section == SECTION_ABS:
                        value = target_value + addend
                        if not -32768 <= value <= 65535:
                            raise LinkError("ABS16 constant out of range")
                    else:
                        raise LinkError("PIC violation: ABS16 to text")
                    patch(code, field, value, 2)
                else:
                    raise LinkError(f"unsupported text reloc type {kind}")
            else:
                slot = data_bases[index] + offset
                if kind != RELOC_ABS32:
                    raise LinkError(
                        f"unsupported data reloc type {kind}")
                if target_section == SECTION_ABS:
                    patch(data, slot, target_value + addend, 4)
                    continue
                if target_section == SECTION_TEXT:
                    record = (slot, 1, M1_HEADER_SIZE + resolved + addend)
                else:
                    record = (slot, 2, resolved + addend)
                if slot in fixup_targets:
                    raise LinkError(f"duplicate fixup at slot {slot}")
                fixup_targets.add(slot)
                fixups.append(record)

    code_size = (text_total + 3) & ~3
    code += bytes(code_size - text_total)
    init_size = max(4, data_total)
    init = bytes(data) + bytes(init_size - data_total)
    return {"code": bytes(code), "init": init, "code_size": code_size,
            "init_size": init_size, "bss_size": bss_total,
            "data_total": data_total, "fixups": fixups}


def expected_s9m1(layout, name):
    name_bytes = name.encode("ascii")
    fixup_bytes = b"".join(struct.pack(">III", target, kind, value)
                           for target, kind, value in layout["fixups"])
    image_size = M1_HEADER_SIZE + layout["code_size"] + \
        layout["init_size"] + len(name_bytes) + len(fixup_bytes)
    image = bytearray(image_size)
    image[0:4] = b"S9M1"
    patch(image, 4, 1, 2)                                  # ABI version
    patch(image, 6, M1_HEADER_SIZE, 2)                     # header size
    patch(image, 8, M1_FLAG_PIC | M1_FLAG_REENTRANT, 4)    # flags
    patch(image, 0x0c, image_size, 4)
    patch(image, 0x10, M1_HEADER_SIZE, 4)                  # code offset
    patch(image, 0x14, layout["code_size"], 4)
    patch(image, 0x18, M1_HEADER_SIZE, 4)                  # entry offset
    patch(image, 0x1c, M1_HEADER_SIZE + layout["code_size"], 4)
    patch(image, 0x20, layout["init_size"], 4)
    patch(image, 0x24, layout["bss_size"], 4)
    image[M1_HEADER_SIZE:M1_HEADER_SIZE + layout["code_size"]] = \
        layout["code"]
    patch(image, 0x28, M1_STACK_SIZE, 4)
    patch(image, 0x2c, M1_HEADER_SIZE + layout["code_size"] +
          layout["init_size"], 4)                          # name offset
    patch(image, 0x30, len(name_bytes), 2)
    image[M1_HEADER_SIZE + layout["code_size"]:
          M1_HEADER_SIZE + layout["code_size"] + layout["init_size"]] = \
        layout["init"]
    name_offset = M1_HEADER_SIZE + layout["code_size"] + layout["init_size"]
    image[name_offset:name_offset + len(name_bytes)] = name_bytes
    fixup_offset = name_offset + len(name_bytes)
    patch(image, 0x38, 0, 4)                               # got offset
    patch(image, 0x3c, M1_GOT_SIZE, 4)                     # got size
    patch(image, 0x40, fixup_offset, 4)
    patch(image, 0x44, len(layout["fixups"]), 4)
    image[fixup_offset:fixup_offset + len(fixup_bytes)] = fixup_bytes
    crc = zlib.crc32(bytes(image[0:0x34]) + b"\0\0\0\0" + bytes(image[0x38:]))
    patch(image, 0x34, crc & 0xffffffff, 4)
    return bytes(image)


def parse_s9m1(path):
    raw = path.read_bytes()
    if len(raw) < M1_HEADER_SIZE or raw[0:4] != b"S9M1":
        raise LinkError(f"{path.name}: not an S9M1 image")
    return raw


def describe_mismatch(expected, actual):
    lines = []
    fields = [(0x0c, "image_size"), (0x14, "code_size"),
              (0x20, "init_size"), (0x24, "bss_size"),
              (0x28, "stack_size"), (0x30, "name_size"),
              (0x3c, "got_size"), (0x44, "fixup_count")]
    for at, label in fields:
        if be32(expected, at) != be32(actual, at):
            lines.append(f"  header {label}: expected "
                         f"{be32(expected, at)}, got {be32(actual, at)}")
    for at, label in [(0x10, "code_offset"), (0x18, "entry_offset"),
                      (0x1c, "data_offset"), (0x2c, "name_offset"),
                      (0x38, "got_offset"), (0x40, "fixup_offset")]:
        if be32(expected, at) != be32(actual, at):
            lines.append(f"  header {label}: expected "
                         f"{be32(expected, at)}, got {be32(actual, at)}")
    code_at = be32(actual, 0x10)
    code_size = be32(actual, 0x14)
    if expected[code_at:code_at + code_size] != \
            actual[code_at:code_at + code_size]:
        lines.append("  code bytes differ")
    data_at = be32(actual, 0x1c)
    init_size = be32(actual, 0x20)
    if expected[data_at:data_at + init_size] != \
            actual[data_at:data_at + init_size]:
        lines.append("  init data bytes differ")
    fixup_offset = be32(actual, 0x40)
    fixup_count = be32(actual, 0x44)
    expected_fixups = sorted(
        struct.unpack_from(">III", expected, fixup_offset + i * 12)
        for i in range((len(expected) - fixup_offset) // 12))
    actual_fixups = sorted(
        struct.unpack_from(">III", actual, fixup_offset + i * 12)
        for i in range(fixup_count))
    if expected_fixups != actual_fixups:
        lines.append(f"  fixups differ: expected {expected_fixups}, "
                     f"got {actual_fixups}")
    stored_crc = be32(actual, 0x34)
    computed = zlib.crc32(bytes(actual[0:0x34]) + b"\0\0\0\0" +
                          bytes(actual[0x38:])) & 0xffffffff
    if stored_crc != computed:
        lines.append(f"  CRC mismatch: stored {stored_crc:#010x}, "
                     f"computed {computed:#010x}")
    return "\n".join(lines)


def link_with_host(link_host, output, inputs, workdir):
    result = run([link_host, workdir / output] +
                 [workdir / name for name in inputs])
    return result


def check_self_vectors(workdir, asm_host, link_host, report):
    failures = 0
    sources = ["start.s", "main.s", "obj.s", "ptr.s"]
    objects = []
    for name in sources:
        out = workdir / (Path(name).stem + ".O")
        result = run([asm_host, LINK_CASES / name, out])
        if result.returncode != 0:
            report(f"FAIL self-vector: asm-host failed on {name}: "
                   f"{result.stderr.strip()}")
            return 1
        objects.append(out)

    # Link orders: input order and reverse-definition order, where obj.s
    # defines helper/factor before main.s references them. Both must match
    # the mini-linker byte for byte (mini_link is link-order independent).
    orders = [("PROG", ["start.O", "main.O", "obj.O"]),
              ("PROGREV", ["start.O", "obj.O", "main.O"]),
              ("PROGPTR", ["start.O", "ptr.O", "obj.O", "main.O"])]
    parsed = {path.name: parse_s9o1(path) for path in objects}
    for stem, inputs in orders:
        try:
            layout = mini_link([parsed[name] for name in inputs])
            expected = expected_s9m1(layout, stem)
        except LinkError as error:
            report(f"FAIL self-vector {stem}: mini-linker error: {error}")
            return failures + 1

        output = workdir / f"{stem}.S9M"
        result = link_with_host(link_host, output.name, inputs, workdir)
        if result.returncode != 0:
            report(f"FAIL self-vector {stem}: link-host failed: "
                   f"{(result.stdout + result.stderr).strip()}")
            return failures + 1
        actual = parse_s9m1(output)
        if actual != expected:
            report(f"FAIL self-vector {stem}: linked {output.name} differs "
                   f"from the expected image:\n"
                   f"{describe_mismatch(expected, actual)}")
            failures += 1

    result = run([asm_host, LINK_CASES / "badmain.s", workdir / "badmain.O"])
    if result.returncode != 0:
        report(f"FAIL negative: asm-host failed on badmain.s: "
               f"{result.stderr.strip()}")
        return failures + 1
    result = link_with_host(link_host, "BAD.S9M", ["badmain.O"], workdir)
    text = (result.stdout + result.stderr).strip()
    if result.returncode == 0:
        report("FAIL negative: undefined symbol was accepted")
        failures += 1
    elif "Undefined symbol" not in text:
        report(f"FAIL negative: missing diagnostic: {text}")
        failures += 1
    if (workdir / "BAD.S9M").exists():
        report("FAIL negative: failed link left an output file")
        failures += 1
    for tag, section, addend in (("code", "text", "-65536"),
                                 ("data", "data", "+65536")):
        source = workdir / f"bad-{tag}.s"
        obj = f"bad-{tag}.O"
        output = f"bad-{tag}.S9M"
        source.write_text(f".{section}\nbase:\n.dc.l 0\n"
                          f".data\n.dc.l base{addend}\n")
        result = run([asm_host, source, workdir / obj])
        if result.returncode != 0:
            report(f"FAIL {tag} range: asm-host failed: {result.stderr.strip()}")
            failures += 1
            continue
        result = link_with_host(link_host, output, [*orders[0][1], obj], workdir)
        if result.returncode == 0 or (workdir / output).exists():
            report(f"FAIL {tag} range: out-of-range fixup was accepted")
            failures += 1
        elif "Relocation value out of range" not in result.stdout + result.stderr:
            report(f"FAIL {tag} range: unexpected diagnostic: "
                   f"{(result.stdout + result.stderr).strip()}")
            failures += 1
    report("self-vector: PROG, PROGREV (definition-first order) and "
           "PROGPTR (data pointer addends) match the mini-linker; "
           "negative case rejected")
    return failures


def check_cross_cases(workdir, asm_host, link_host, report):
    failures = 0
    checked = 0
    golden_root = LINK_CASES / "golden"
    for case_dir in sorted(path for path in golden_root.iterdir()
                           if path.is_dir()):
        sources = sorted(case_dir.glob("*.s"))
        if len(sources) < 2:
            report(f"SKIP {case_dir.name}: needs at least two sources")
            continue
        objects = []
        elf_objects = []
        for source in sources:
            out = workdir / f"{case_dir.name}-{source.stem}.O"
            result = run([asm_host, source, out])
            if result.returncode != 0:
                report(f"FAIL {case_dir.name}: asm-host failed on "
                       f"{source.name}: {result.stderr.strip()}")
                return failures + 1
            objects.append(out)
            # m68k-xelf-as gives .text an alignment of 4 and GNU ld pads
            # section gaps with NOPs, so the cross profile also requires a
            # text length that is a multiple of 4 in every object.
            if len(parse_s9o1(out)["text"]) % 4 != 0:
                report(f"FAIL {case_dir.name}: {source.name} text length "
                       f"is not a multiple of 4")
                return failures + 1
            elf = workdir / f"{case_dir.name}-{source.stem}.elf.o"
            result = run(["m68k-xelf-as", "-m68000", source, "-o", elf])
            if result.returncode != 0:
                report(f"FAIL {case_dir.name}: m68k-xelf-as failed on "
                       f"{source.name}: {result.stderr.strip()}")
                return failures + 1
            elf_objects.append(elf)

        output = workdir / f"{case_dir.name}.S9M"
        result = link_with_host(link_host, output.name,
                                [path.name for path in objects], workdir)
        if result.returncode != 0:
            report(f"FAIL {case_dir.name}: link-host failed: "
                   f"{(result.stdout + result.stderr).strip()}")
            return failures + 1
        image = parse_s9m1(output)
        code_offset = be32(image, 0x10)
        code_size = be32(image, 0x14)
        ours = image[code_offset:code_offset + code_size]

        linked_elf = workdir / f"{case_dir.name}.linked.elf"
        result = run(["m68k-xelf-ld", "-o", linked_elf] + elf_objects)
        if result.returncode != 0:
            report(f"FAIL {case_dir.name}: m68k-xelf-ld failed: "
                   f"{result.stderr.strip()}")
            return failures + 1
        binary = workdir / f"{case_dir.name}.gnu.bin"
        result = run(["m68k-xelf-objcopy", "-O", "binary",
                      "--only-section=.text", linked_elf, binary])
        if result.returncode != 0:
            report(f"FAIL {case_dir.name}: objcopy failed: "
                   f"{result.stderr.strip()}")
            return failures + 1
        theirs = binary.read_bytes()
        checked += 1
        if ours[:len(theirs)] != theirs or any(ours[len(theirs):]):
            report(f"FAIL {case_dir.name}: text mismatch (ours "
                   f"{len(ours)} bytes, GNU ld {len(theirs)} bytes)")
            failures += 1
        else:
            report(f"PASS {case_dir.name}: text matches m68k-xelf-ld")
    report(f"golden cross-comparison: {checked - failures}/{checked} "
           f"cases matched")
    return failures


def main():
    asm_host = ROOT / "tests" / "build" / "asm-host"
    link_host = ROOT / "tests" / "build" / "link-host"
    if not asm_host.exists():
        print(f"missing {asm_host}; run make -C tests test-asm-host first",
              file=sys.stderr)
        return 2
    if not link_host.exists():
        print(f"missing {link_host}; the linker host driver is provided by "
              f"apps/link (make -C tests test-link-host)", file=sys.stderr)
        return 2
    missing = [tool for tool in ("m68k-xelf-as", "m68k-xelf-ld",
                                 "m68k-xelf-objcopy")
               if shutil.which(tool) is None]
    if missing:
        print(f"{', '.join(missing)} not found; source ~/.elf2x68k first",
              file=sys.stderr)
        return 2

    def report(message):
        print(message)

    failures = 0
    with tempfile.TemporaryDirectory() as tmp:
        workdir = Path(tmp)
        failures += check_self_vectors(workdir, asm_host, link_host, report)
        failures += check_cross_cases(workdir, asm_host, link_host, report)
    if failures:
        print(f"link golden: {failures} failure(s)")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
