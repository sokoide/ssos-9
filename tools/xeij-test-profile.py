#!/usr/bin/env python3
"""Create a disposable XEiJ configuration and clone its attached media."""

import argparse
import json
import re
import shutil
from pathlib import Path


def prepare(source, export, output):
    source = source.resolve(strict=True)
    stage = output / "hfs"
    media = output / "media"
    config = output / "XEiJ.ini"
    stage.mkdir()
    media.mkdir()
    for name in ("ssos9.x", "ssos9.xdf"):
        shutil.copyfile(export / name, stage / name)

    overrides = {
        "hf0": stage,
        "pastepipe": "on",
        "saveonexit": "off",
        "rs232cconnection": "",
    }
    lines = []
    cloned_media = {}
    for line in source.read_text().splitlines():
        key, separator, value = line.partition("=")
        key = key.strip().lower()
        if key in overrides or re.fullmatch(r"hf\d+", key):
            continue
        if separator and value and re.fullmatch(r"(?:fd|hd|sa|sc|se|si)\d+", key):
            readonly = ":R" if value.endswith(":R") else ""
            filename = value[:-2] if readonly else value
            path = Path(filename).expanduser()
            if not path.is_absolute():
                raise ValueError(f"{key} needs an absolute image path: {path}")
            clone = media / f"{key}{path.suffix}"
            shutil.copyfile(path, clone)
            cloned_media[str(clone)] = str(path.resolve(strict=True))
            line = f"{key}={clone}{readonly}"
        lines.append(line)
    lines.extend(f"{key}={value}" for key, value in overrides.items())
    config.write_text("\n".join(lines) + "\n")
    manifest = {"source_ini": str(source), "media": cloned_media}
    (output / "profile.json").write_text(json.dumps(manifest, indent=2) + "\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source_ini", type=Path)
    parser.add_argument("export_directory", type=Path)
    parser.add_argument("new_profile_directory", type=Path)
    args = parser.parse_args()
    output = args.new_profile_directory.resolve()
    try:
        output.mkdir(mode=0o700)
        try:
            prepare(args.source_ini, args.export_directory, output)
        except (OSError, ValueError):
            shutil.rmtree(output)
            raise
    except (OSError, ValueError) as error:
        parser.exit(1, f"XEiJ profile: {error}\n")
    print(output / "XEiJ.ini")


if __name__ == "__main__":
    main()
