#!/usr/bin/env python3
"""Create a separate SSOS-readable, single-file FAT12 2HD image."""

import argparse
import re
import struct
import sys
from pathlib import Path

SECTOR = 1024
SECTORS = 1232
FAT_START = 1
FAT_SECTORS = 2
ROOT_START = 5
ROOT_SECTORS = 6
DATA_START = ROOT_START + ROOT_SECTORS


def create(data, name):
    match = re.fullmatch(r"([A-Za-z0-9_-]{1,8})\.([A-Za-z0-9_-]{1,3})", name)
    if not match:
        raise ValueError("filename must be ASCII 8.3")
    if not data:
        raise ValueError("empty file")
    clusters = (len(data) + SECTOR - 1) // SECTOR
    if clusters > SECTORS - DATA_START:
        raise ValueError("file exceeds disk capacity")
    image = bytearray(SECTORS * SECTOR)
    image[0:3] = b"\xeb\x3c\x90"
    image[3:11] = b"SSOS9   "
    struct.pack_into("<HBHBHHBHHHII", image, 11, SECTOR, 1, 1, 2,
                     ROOT_SECTORS * SECTOR // 32, SECTORS, 0xfe,
                     FAT_SECTORS, 8, 2, 0, 0)
    image[38] = 0x29
    image[43:54] = b"NO NAME    "
    image[54:62] = b"FAT12   "
    image[510:512] = b"\x55\xaa"
    fat = bytearray(FAT_SECTORS * SECTOR)
    fat[:3] = b"\xfe\xff\xff"
    for cluster in range(2, clusters + 2):
        value = 0xfff if cluster == clusters + 1 else cluster + 1
        pos = cluster + cluster // 2
        pair = fat[pos] | (fat[pos + 1] << 8)
        pair = ((pair & 0x000f) | (value << 4)) if cluster & 1 else ((pair & 0xf000) | value)
        fat[pos:pos + 2] = struct.pack("<H", pair)
    for index in range(2):
        start = (FAT_START + index * FAT_SECTORS) * SECTOR
        image[start:start + len(fat)] = fat
    root = ROOT_START * SECTOR
    base, ext = (part.upper().encode("ascii") for part in match.groups())
    image[root:root + 11] = base.ljust(8, b" ") + ext.ljust(3, b" ")
    image[root + 11] = 0x20
    struct.pack_into("<H", image, root + 24, 0x21)  # 1980-01-01
    struct.pack_into("<H", image, root + 26, 2)
    struct.pack_into("<I", image, root + 28, len(data))
    image[DATA_START * SECTOR:DATA_START * SECTOR + len(data)] = data
    return image


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--name", default="HELLO.S9M")
    args = parser.parse_args()
    try:
        args.output.write_bytes(create(args.source.read_bytes(), args.name))
    except (OSError, ValueError) as exc:
        print(f"mkfat12xdf: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
