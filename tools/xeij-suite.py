#!/usr/bin/env python3
"""Run selected XEiJ cases serially and retain first-attempt evidence."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import tempfile
import time


def checksum(path):
    if not path.is_file():
        return None
    digest = hashlib.sha256()
    with path.open("rb") as file:
        for chunk in iter(lambda: file.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()



def profile_evidence(ini):
    manifest = ini.parent / "profile.json"
    if not manifest.is_file():
        return {}, {}
    profile = json.loads(manifest.read_text())
    originals = set(profile["media"].values()) | {profile["source_ini"]}
    return (
        {name: checksum(Path(name)) for name in sorted(originals)},
        {name: checksum(Path(name)) for name in sorted(profile["media"])},
    )

def check_disposable_profile(ini, stage):
    root = ini.parent.resolve()
    manifest = json.loads((root / "profile.json").read_text())
    if (ini.resolve() != root / "XEiJ.ini" or ini.is_symlink()
            or ini.samefile(manifest["source_ini"])):
        raise ValueError("the active ini must be the disposable XEiJ.ini")
    if stage.resolve() != root / "hfs":
        raise ValueError("the HFS share must be the disposable hfs directory")
    media = set()
    for clone, source in manifest["media"].items():
        image = Path(clone)
        if image.resolve().parent != root / "media" or image.samefile(source):
            raise ValueError(f"media is not an independent disposable copy: {clone}")
        media.add(image.resolve())
    attached = set()
    hfs = []
    for line in ini.read_text().splitlines():
        key, _, value = (part.strip() for part in line.partition("="))
        if value.endswith(":R"):
            value = value[:-2]
        if value and re.fullmatch(r"(?:fd|hd|sa|sc|se|si)\d+", key.lower()):
            attached.add(Path(value).resolve())
        if value and re.fullmatch(r"hf\d+", key.lower()):
            hfs.append((key.lower(), Path(value).resolve()))
    if attached != media or hfs != [("hf0", stage.resolve())]:
        raise ValueError("ini media or HFS shares differ from the disposable profile")



def save_report(run_dir, report):
    temporary = run_dir / "result.tmp"
    temporary.write_text(json.dumps(report, indent=2) + "\n")
    temporary.replace(run_dir / "result.json")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--results", type=Path, default=Path("build/xeij/results"))
    parser.add_argument("--require-profile", action="store_true")
    parser.add_argument("--mfp-check", action="store_true",
                        help="require the Human68k MFP snapshot probe for a2")
    parser.add_argument("cases", nargs="+")
    args = parser.parse_args()
    if any(re.fullmatch(r"[a-h][1-6]", case) is None for case in args.cases):
        parser.error("invalid case ID")

    if args.mfp_check and "a2" not in args.cases:
        parser.error("--mfp-check requires a2")
    mfp_check = args.mfp_check or os.environ.get("SSOS9_XEIJ_MFP_CHECK") == "1"

    stage = Path(os.environ.get("SSOS9_XEIJ_STAGE", Path.home() / "tmp"))
    ini = Path(os.environ.get("SSOS9_XEIJ_INI", Path.home() / "XEiJ.ini"))
    if args.require_profile:
        try:
            check_disposable_profile(ini, stage)
        except (OSError, ValueError, KeyError) as error:
            parser.error(f"invalid disposable profile: {error}")

    suite_start = time.monotonic()
    args.results.mkdir(parents=True, exist_ok=True)
    run_dir = Path(tempfile.mkdtemp(dir=args.results)).resolve()
    print(f"XEiJ results: {run_dir}", flush=True)
    runner_dir = run_dir / "runner"
    runner_dir.mkdir()
    for name in ("emulator-itest.sh", "xeij-transport.py"):
        shutil.copy2(Path(__file__).with_name(name), runner_dir / name)

    artifacts = {
        path.name: checksum(path)
        for path in (stage / "ssos9.x", stage / "ssos9.xdf", stage / "mfpstate.x")
    }
    restoration = {"before": checksum(ini)}
    originals, media = profile_evidence(ini)
    emulator = {
        key: checksum(Path(os.environ[key]))
        for key in ("SSOS9_XEIJ_JAR", "SSOS9_XEIJ_CGROM")
        if os.environ.get(key)
    }
    report = {
        "host": platform.platform(),
        "artifacts": artifacts,
        "emulator_artifacts": emulator,
        "ini": restoration,
        "originals_before": originals,
        "media_before": media,
        "runner": {path.name: checksum(path) for path in sorted(runner_dir.iterdir())},
        "cases": [{"case": case, "status": "not_run"} for case in args.cases],
    }
    save_report(run_dir, report)
    exit_code = 0
    for index, result in enumerate(report["cases"], 1):
        if exit_code:
            break
        case = result["case"]
        result["status"] = "running"
        if case == "a2":
            result["mfp_check"] = mfp_check
        save_report(run_dir, report)
        stem = f"{index:02d}-{case}"
        log = run_dir / f"{stem}.log"
        env = {
            **os.environ,
            "SSOS9_XEIJ_MFP_CHECK": "1" if mfp_check else "0",
            "SSOS9_XEIJ_COMMAND_LOG": str(run_dir / f"{stem}.keys.log"),
            "SSOS9_XEIJ_SCREEN_LOG": str(run_dir / f"{stem}.screen.txt"),
        }
        print(f"== {case} ==", flush=True)
        started = time.monotonic()
        with log.open("wb", buffering=0) as output, subprocess.Popen(
            [runner_dir / "emulator-itest.sh", case],
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            env=env,
        ) as child:
            for line in child.stdout:
                output.write(line)
                print(line.decode("utf-8", "replace"), end="", flush=True)
            exit_code = child.wait()
        status = {0: "pass", 3: "environment_error", 4: "not_run"}.get(exit_code, "fail")
        result.update(status=status, exit_code=exit_code, seconds=time.monotonic() - started, log=log.name)
        save_report(run_dir, report)
    restoration["after"] = checksum(ini)
    report["originals_after"], report["media_after"] = profile_evidence(ini)
    if restoration["before"] != restoration["after"] or originals != report["originals_after"]:
        print("XEiJ configuration or original media changed during run", flush=True)
        if not exit_code:
            exit_code = 3
    report["seconds"] = time.monotonic() - suite_start
    report["exit_code"] = exit_code
    save_report(run_dir, report)
    return exit_code if exit_code >= 0 else 128 - exit_code


if __name__ == "__main__":
    raise SystemExit(main())
