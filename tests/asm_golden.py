#!/usr/bin/env python3
"""Golden test: compare our assembler's text bytes with m68k-xelf-as.

Case sources under tests/asm/cases/*.s use the syntax subset shared with GAS
(GAS-compatible directives only), so the same file assembles with both.
Error cases (*.err.s) must fail in our assembler.
"""

import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CASES = ROOT / "tests" / "asm" / "cases"


def run(command, **kwargs):
    return subprocess.run(command, capture_output=True, text=True, **kwargs)


WIDTHS = {"R_68K_8": 1, "R_68K_16": 2, "R_68K_32": 4}


def apply_defined_relocs(elf, data):
    """Fold GAS relocations against defined (section) symbols into the text
    bytes. The contract stores the resolved section-relative value in the
    field bytes, while GAS stores 0 and relocates against the section
    symbol with the value as addend; folding makes the two byte streams
    comparable. Relocations against undefined (extern) symbols are left
    alone: both assemblers write 0 there."""
    result = run(["m68k-xelf-readelf", "-rW", str(elf)])
    if result.returncode != 0:
        return f"m68k-xelf-readelf -rW failed: {result.stderr.strip()}"
    for line in result.stdout.splitlines():
        parts = line.split()
        if len(parts) < 7 or not parts[2].startswith("R_68K_"):
            continue
        rtype = parts[2]
        if rtype not in WIDTHS:
            continue  # PC-relative forms resolve inside GAS itself
        target = parts[4]
        if not target.startswith("."):
            continue  # extern symbol: field stays 0 on both sides
        op = parts[5]
        try:
            addend = int(parts[6], 16)  # readelf always prints hex ("+ c")
        except ValueError:
            return f"unparsable relocation addend: {line.strip()}"
        value = addend if op == "+" else -addend
        offset = int(parts[0], 16)
        width = WIDTHS[rtype]
        if offset + width > len(data):
            return f"relocation offset {offset} outside .text: {line.strip()}"
        chunk = bytearray(data[offset:offset + width])
        acc = 0
        for i, b in enumerate(chunk):
            acc = (acc << 8) | b
        acc = (acc + value) & ((1 << (width * 8)) - 1)
        for i in range(width):
            data[offset + width - 1 - i] = (acc >> (8 * i)) & 0xFF
    return None


def our_object(case, workdir, asm_host):
    out = workdir / (case.stem + ".o")
    result = run([str(asm_host), str(case), str(out)])
    return result, out


def gas_text(case, workdir):
    elf = workdir / (case.stem + ".elf")
    binfile = workdir / (case.stem + ".bin")
    result = run(["m68k-xelf-as", "-m68000", str(case), "-o", str(elf)])
    if result.returncode != 0:
        return None, f"m68k-xelf-as failed: {result.stderr.strip()}"
    result = run(["m68k-xelf-objcopy", "-O", "binary", "--only-section=.text",
                  str(elf), str(binfile)])
    if result.returncode != 0:
        return None, f"objcopy failed: {result.stderr.strip()}"
    data = bytearray(binfile.read_bytes())
    error = apply_defined_relocs(elf, data)
    if error:
        return None, error
    return bytes(data), None


def s9ob_text(path, workdir):
    result = run([sys.executable, str(ROOT / "tools" / "s9ob.py"),
                  "--text-only", str(path)])
    if result.returncode != 0:
        return None, f"s9ob failed: {result.stdout.strip()} {result.stderr.strip()}"
    return bytes.fromhex(result.stdout.strip()), None


def main():
    asm_host = ROOT / "tests" / "build" / "asm-host"
    if not asm_host.exists():
        print(f"missing {asm_host}; build tests first", file=sys.stderr)
        return 2
    if shutil.which("m68k-xelf-as") is None:
        print("m68k-xelf-as not found; source ~/.elf2x68k first", file=sys.stderr)
        return 2
    failures = 0
    checked = 0
    with tempfile.TemporaryDirectory() as tmp:
        workdir = Path(tmp)
        for case in sorted(CASES.glob("*.s")):
            expect_error = case.stem.endswith(".err")
            result, out = our_object(case, workdir, asm_host)
            if expect_error:
                checked += 1
                if result.returncode == 0:
                    print(f"FAIL {case.name}: expected an error, got success")
                    failures += 1
                elif "error" not in result.stderr and ":" not in result.stderr:
                    print(f"FAIL {case.name}: error lacks file:line prefix: "
                          f"{result.stderr.strip()}")
                    failures += 1
                continue
            if result.returncode != 0:
                print(f"FAIL {case.name}: {result.stderr.strip()}")
                failures += 1
                continue
            ours, error = s9ob_text(out, workdir)
            if error:
                print(f"FAIL {case.name}: {error}")
                failures += 1
                continue
            theirs, error = gas_text(case, workdir)
            if error:
                print(f"FAIL {case.name}: {error}")
                failures += 1
                continue
            checked += 1
            if ours != theirs:
                print(f"FAIL {case.name}: text mismatch "
                      f"(ours {len(ours)} bytes, gas {len(theirs)} bytes)")
                failures += 1
    print(f"asm golden: {checked - failures}/{checked} cases matched")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
