#!/usr/bin/env python3
"""Make a disposable Human68k-compatible floppy for the console smoke test.

The image is a copy of a template 2HD floppy with the standalone launcher
(SS9CON.X), the console module (CONSOLE.S9M), and any extra modules placed
on it. Pass --background tests/target/build/BG.S9M to include the
background test. Pass --module (repeatable) to place additional S9M1
modules; each is stored under an uppercase 8.3 name derived from its file
stem, e.g. --module apps/edit/build/edit.s9m -> EDIT.S9M.

Typical use:
  python3 tests/target/prepare_console_image.py \
      tests/target/build/console.xdf build/h2/console.xdf \
      ssos/build/export/ssos9.x tests/target/build/console.s9m \
      --module apps/edit/build/edit.s9m

The helper preserves the template and adjusts the copied entries for
Human68k; the resulting floppy is intended as a data disk, not an IPL
boot disk.
"""
import argparse
import re
import shutil
import struct
import subprocess
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument("template", type=Path)
parser.add_argument("output", type=Path)
parser.add_argument("launcher", type=Path)
parser.add_argument("module", type=Path)
parser.add_argument("--background", type=Path)
parser.add_argument("--module", dest="modules", type=Path, action="append",
                    default=[], metavar="PATH",
                    help="extra S9M1 module to place (repeatable); stored "
                         "as an uppercase 8.3 name derived from the stem")
args = parser.parse_args()
if args.template.resolve() == args.output.resolve():
    parser.error("template and output must differ")
files = [(args.launcher, "SS9CON.X", b"SS9CON  X  "),
         (args.module, "CONSOLE.S9M", b"CONSOLE S9M")]
if args.background:
    files.append((args.background, "BG.S9M", b"BG      S9M"))
for extra in args.modules:
    stem = re.sub(r"[^A-Z0-9_]", "", extra.stem.upper())[:8] or "MODULE"
    entry_name = f"{stem:<8}S9M".encode("ascii")
    if any(existing == entry_name for _, _, existing in files):
        parser.error(f"module name collision for {stem} ({extra})")
    files.append((extra, f"{stem}.S9M", entry_name))
shutil.copyfile(args.template, args.output)
for src, name, _ in files:
    subprocess.run(["mcopy", "-o", "-i", str(args.output), str(src), f"::{name}"], check=True)
image = bytearray(args.output.read_bytes())
bps = struct.unpack_from("<H", image, 11)[0]
reserved = struct.unpack_from("<H", image, 14)[0]
fats = image[16]
entries = struct.unpack_from("<H", image, 17)[0]
fat_sectors = struct.unpack_from("<H", image, 22)[0]
root = (reserved + fats * fat_sectors) * bps
if bps not in (512, 1024) or root + entries * 32 > len(image):
    raise SystemExit("invalid FAT root directory")
for _, _, name in files:
    matches = [root + i * 32 for i in range(entries)
               if image[root + i * 32:root + i * 32 + 11] == name]
    if len(matches) != 1:
        raise SystemExit(f"expected one directory entry for {name!r}")
    # Human68k uses the MS-DOS reserved bytes as a filename extension.
    image[matches[0] + 12:matches[0] + 22] = bytes(10)
# SSOS-9's FAT12 loader requires the conventional boot-sector signature.
# This output is a data floppy, not a bootable system disk.
image[510:512] = b"\x55\xaa"
args.output.write_bytes(image)
