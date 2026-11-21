#!/usr/bin/env python3
"""Extract root directory and FAT16 evidence from a stopped XEiJ HDS clone."""
import argparse
import hashlib
import json
from pathlib import Path


def extract(image, prefix):
    def be(data):
        return int.from_bytes(data, "big")

    with image.open("rb") as disk:
        def read(offset, size):
            disk.seek(offset)
            data = disk.read(size)
            if len(data) != size:
                raise ValueError("truncated image")
            return data

        table = read(2048, 32)
        if read(0, 8) != b"X68SCSI1" or table[:4] != b"X68K" or table[16:24] != b"Human68k":
            raise ValueError("expected an X68SCSI1 disk with a first Human68k partition")
        start = (be(table[24:28]) & 0xFFFFFF) * 1024
        boot = read(start, 1024)
        if be(boot[0x12:0x14]) != 1024 or boot[0x1C] != 0xF7:
            raise ValueError("expected a 1024-byte Human68k FAT16 volume")
        cluster_bytes = boot[0x14] * 1024
        fat_count = boot[0x15] & 0x7F
        byte_order = "little" if boot[0x15] & 0x80 else "big"
        fat_size = boot[0x1D] * 1024
        fat_start = start + be(boot[0x16:0x18]) * 1024
        root_size = be(boot[0x18:0x1A]) * 32
        if not all((cluster_bytes, fat_count, fat_size, root_size)):
            raise ValueError("invalid FAT16 geometry")
        fats = [read(fat_start + i * fat_size, fat_size) for i in range(fat_count)]
        root_start = fat_start + fat_count * fat_size
        root = read(root_start, root_size)
        data_start = root_start + (root_size + 1023) // 1024 * 1024
        entries = []
        for offset in range(0, len(root), 32):
            row = root[offset:offset + 32]
            if not row[0]:
                break
            if not row[:8].lower().startswith(prefix.lower()):
                continue
            cluster = int.from_bytes(row[26:28], "little")
            remaining = int.from_bytes(row[28:32], "little")
            payload = hashlib.sha256()
            chain = []
            seen = set()
            while remaining:
                if cluster < 2 or cluster in seen or cluster * 2 + 2 > fat_size:
                    raise ValueError("invalid FAT16 chain")
                seen.add(cluster)
                fat_words = [fat[cluster * 2:cluster * 2 + 2] for fat in fats]
                chain.append({"cluster": cluster, "fat_words": [word.hex() for word in fat_words]})
                take = min(remaining, cluster_bytes)
                payload.update(read(data_start + (cluster - 2) * cluster_bytes, take))
                remaining -= take
                cluster = int.from_bytes(fat_words[0], byte_order)
            if chain and cluster < 0xFFF8:
                raise ValueError("expected end-of-chain after file payload")
            entries.append({"offset": root_start + offset, "entry_hex": row.hex(),
                            "payload_sha256": payload.hexdigest(), "chain": chain})
        if not entries:
            raise ValueError("no matching entries")
        return {"fat_order": byte_order, "cluster_bytes": cluster_bytes,
                "fat_sha256": [hashlib.sha256(fat).hexdigest() for fat in fats],
                "entries": entries}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", type=Path)
    parser.add_argument("prefix", help="ASCII prefix of the root entries, up to eight characters")
    args = parser.parse_args()
    if not args.prefix.isascii() or not 1 <= len(args.prefix) <= 8:
        parser.error("expected an ASCII prefix of one to eight characters")
    try:
        print(json.dumps(extract(args.image, args.prefix.encode()), indent=2))
    except (OSError, ValueError) as error:
        parser.exit(1, f"{error}\n")


if __name__ == "__main__":
    main()
