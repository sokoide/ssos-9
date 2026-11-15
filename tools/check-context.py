#!/usr/bin/env python3
"""Keep the production Timer-D context-switch path in sync with the QEMU port.

Compares the FULL bodies (global label to the next global label, including
local labels and every basic block) of ss_task_yield, ss_context_switch, and
ss_timerd_handler between:

  production: ssos/os/kernel/preemptive/interrupts.s
  QEMU port:  tests/qemu/pre/preempt_ctx_switch.s

after comment stripping and light normalization (case, '%', spacing, and the
bra/bsr .w suffix width). Everything else must match verbatim; in particular
no I/O or other instruction lines are excluded.

Sanctioned differences (the only two allowed):
1. Timer-D EOI.  Production clears the MFP in-service bit (ISRB 0xe88011,
   bit 4) with an exact 4-instruction sequence on every exit path; the QEMU
   port has no MFP and instead re-enables interrupts (move.w #0x2000,%sr)
   right before each early-exit rte.  Matching EOI windows are canonicalized
   to one token and dropped, QEMU re-enable lines directly before an rte are
   dropped, and the derived counts are asserted:
     EOI count == exit paths (rte + tail jump to a global) in each handler
     and QEMU re-enable count == QEMU rte count.
   An EOI that is altered loses its canonical shape and stays in the body,
   so the comparison fails; an EOI that is deleted breaks the count assert.
2. Switch threshold.  Production hardcodes cmpi.b #10 while the QEMU port
   uses the build-time symbol SS_TIMERD_SWITCH_TICKS (default 1, overridden
   with --defsym for the cadence test).  The symbol is substituted with the
   immediate derived from the production cmpi.b, so a threshold-value-only
   change on either side is not flagged (the cadence test pins the value).

With --selftest, in-memory mutations of the real files (instruction change,
label deletion, EOI alteration, EOI deletion, QEMU re-enable deletion) must
each be detected; the script exits nonzero if one slips through or if the
unmutated comparison fails.
"""
from pathlib import Path
import re
import sys

root = Path(__file__).resolve().parent.parent

PRODUCTION = "ssos/os/kernel/preemptive/interrupts.s"
QEMU = "tests/qemu/pre/preempt_ctx_switch.s"
COMPARED_LABELS = ("ss_task_yield", "ss_context_switch", "ss_timerd_handler")

EOI_TOKEN = "<timer-d-eoi>"
EOI_TARGET = "0xe88011"
EOI_MASK = "0xef"
REENABLE = "move.w #0x2000,sr"
THRESHOLD_SYMBOL = "#ss_timerd_switch_ticks"

GLOBAL_LABEL = re.compile(r"^[A-Za-z_]\w*:$")
EOI_LOAD = re.compile(r"(?:move\.l #0xe88011|lea 0xe88011),a\d")
EOI_READ = re.compile(r"move\.b \(a\d\),d\d")
EOI_AND = re.compile(r"andi\.b #0xef,d\d")
EOI_WRITE = re.compile(r"move\.b d\d,\(a\d\)")
TAIL_JUMP = re.compile(r"^(?:bra|jmp) [A-Za-z_]\w*$")
PROD_THRESHOLD = re.compile(r"^cmpi\.b #(\d+),\(a0\)$")


class CheckError(Exception):
    pass


def normalize(line):
    """Strip comments and fold casing/spacing so reformatting is not a diff."""
    line = line.split("|")[0].strip()
    line = re.sub(r"\s+", " ", line).lower().replace("%", "")
    line = re.sub(r",\s*", ",", line)
    line = re.sub(r"\b(bsr|bra)\.w\b", r"\1", line)
    return line


def extract_function(lines, label, filename):
    """Collect a full function body: global label until the next global
    label, directive, or preprocessor line. Local labels stay in the body so
    their deletion is a diff."""
    body = []
    active = False
    for raw in lines:
        line = raw.split("|")[0].strip()
        if not active:
            if line == label + ":":
                active = True
                body.append(line)
            continue
        if GLOBAL_LABEL.match(line):
            break
        if not line:
            continue
        if line.startswith("#"):
            break
        if line.startswith("."):
            if line.endswith(":"):
                body.append(line)
            else:
                break
            continue
        body.append(line)
    if not active:
        raise CheckError(f"{filename}: label {label} not found")
    if len(body) <= 1:
        raise CheckError(f"{filename}: empty body for {label}")
    return body


def canonicalize_eoi(seq):
    """Replace exact 4-instruction Timer-D EOI windows (load the ISRB
    address into an address register, read, clear bit 4, write back) with a
    single token. Anything that deviates from the shape stays verbatim."""
    out = []
    count = 0
    i = 0
    while i < len(seq):
        if (i + 3 < len(seq) and EOI_LOAD.fullmatch(seq[i])
                and EOI_READ.fullmatch(seq[i + 1])
                and EOI_AND.fullmatch(seq[i + 2])
                and EOI_WRITE.fullmatch(seq[i + 3])):
            load, read, mask, write = seq[i:i + 4]
            address = re.search(r",(a\d)$", load).group(1)
            data = re.search(r",(d\d)$", read).group(1)
            if read != f"move.b ({address}),{data}" or mask != f"andi.b #0xef,{data}" or write != f"move.b {data},({address})":
                out.append(seq[i])
                i += 1
                continue
            out.append(EOI_TOKEN)
            count += 1
            i += 4
            continue
        out.append(seq[i])
        i += 1
    return out, count


def derive_threshold(seq, filename):
    for line in seq:
        match = PROD_THRESHOLD.fullmatch(line)
        if match:
            return match.group(1)
    raise CheckError(
        f"{filename}: cannot derive the switch-tick threshold "
        f"(expected cmpi.b #N,(a0) in ss_timerd_handler)")


def substitute_threshold(seq, value):
    return [line.replace(THRESHOLD_SYMBOL, "#" + value) for line in seq]


def drop_eoi_tokens(seq):
    return [line for line in seq if line != EOI_TOKEN]


def drop_exit_reenable(seq):
    """QEMU-side sanctioned lines: the interrupt re-enable right before an
    early-exit rte (the port's replacement for the MFP EOI write)."""
    out = []
    for i, line in enumerate(seq):
        if line == REENABLE and i + 1 < len(seq) and seq[i + 1] == "rte":
            continue
        out.append(line)
    return out


def exit_paths(seq):
    """Every path leaving the ISR body: rte or a tail branch/jump to a
    global symbol. Local branches stay inside the body."""
    return sum(1 for line in seq
               if line == "rte" or TAIL_JUMP.match(line))


def first_difference(a, b):
    for i in range(max(len(a), len(b))):
        left = a[i] if i < len(a) else "<missing>"
        right = b[i] if i < len(b) else "<missing>"
        if left != right:
            return i, left, right
    return None


def compare(production_lines, qemu_lines):
    """Returns (report lines, error lines)."""
    report = []
    errors = []

    prod_handler = extract_function(production_lines, "ss_timerd_handler",
                                    PRODUCTION)
    qemu_handler = extract_function(qemu_lines, "ss_timerd_handler", QEMU)

    prod_norm = [normalize(l) for l in prod_handler]
    qemu_norm = [normalize(l) for l in qemu_handler]

    threshold = derive_threshold(prod_norm, PRODUCTION)
    qemu_norm = substitute_threshold(qemu_norm, threshold)

    prod_eoi, prod_eoi_count = canonicalize_eoi(prod_norm)
    qemu_eoi, qemu_eoi_count = canonicalize_eoi(qemu_norm)

    prod_paths = exit_paths(prod_norm)
    qemu_paths = exit_paths(qemu_norm)
    qemu_reenables = sum(1 for line in qemu_norm if line == REENABLE)
    qemu_rtes = sum(1 for line in qemu_norm if line == "rte")

    report.append(
        f"ss_timerd_handler: production EOI sequences={prod_eoi_count}, "
        f"exit paths={prod_paths}; QEMU EOI sequences={qemu_eoi_count}, "
        f"re-enable-before-rte={qemu_reenables}, rte={qemu_rtes}, "
        f"exit paths={qemu_paths}; switch threshold={threshold}")

    if prod_eoi_count != prod_paths:
        errors.append(
            f"production ss_timerd_handler has {prod_eoi_count} canonical "
            f"EOI sequences but {prod_paths} exit paths; every exit path "
            f"must clear the MFP in-service bit")
    if qemu_rtes != qemu_reenables:
        errors.append(
            f"QEMU ss_timerd_handler has {qemu_rtes} rte but "
            f"{qemu_reenables} interrupt re-enable lines; every early-exit "
            f"path must re-enable interrupts before rte")
    if prod_eoi_count == 0:
        errors.append("production ss_timerd_handler lost every EOI sequence")

    prod_cmp = drop_exit_reenable(drop_eoi_tokens(prod_eoi))
    qemu_cmp = drop_exit_reenable(drop_eoi_tokens(qemu_eoi))
    diff = first_difference(prod_cmp, qemu_cmp)
    if diff is not None:
        index, left, right = diff
        errors.append(
            f"ss_timerd_handler differs at line {index}: "
            f"production={left!r} QEMU={right!r}")
    else:
        report.append(
            f"ss_timerd_handler: identical ({len(prod_cmp)} compared "
            f"lines) after sanctioned EOI/normalization")

    for label in COMPARED_LABELS:
        if label == "ss_timerd_handler":
            continue
        prod_body = [normalize(l)
                     for l in extract_function(production_lines, label,
                                               PRODUCTION)]
        qemu_body = substitute_threshold(
            [normalize(l)
             for l in extract_function(qemu_lines, label, QEMU)], threshold)
        _, prod_count = canonicalize_eoi(prod_body)
        if prod_count:
            errors.append(
                f"{label}: EOI sequence found outside ss_timerd_handler")
        diff = first_difference(prod_body, qemu_body)
        if diff is not None:
            index, left, right = diff
            errors.append(
                f"{label} differs at line {index}: "
                f"production={left!r} QEMU={right!r}")
        else:
            report.append(
                f"{label}: identical ({len(prod_body)} lines)")

    return report, errors


MUTATIONS = (
    ("instruction change",
     lambda lines: replace_once(lines, "addq.l  #1, ss_tick_counter",
                                "addq.l  #2, ss_tick_counter")),
    ("label deletion",
     lambda lines: delete_once(lines, ".no_switch:")),
    ("EOI alteration",
     lambda lines: replace_once(lines, "andi.b #0xef, d1",
                                "andi.b #0xff, d1")),
    ("EOI register alteration",
     lambda lines: replace_once(lines, "andi.b #0xef, d1",
                                "andi.b #0xef, d2")),
    ("EOI deletion",
     lambda lines: delete_block(lines, ["move.l #0xe88011, a0",
                                        "move.b (a0), d1",
                                        "andi.b #0xef, d1",
                                        "move.b d1, (a0)"])),
    ("QEMU re-enable deletion",
     lambda lines: delete_once_after(lines, "ss_timerd_handler:",
                                     "move.w  #0x2000, %sr")),
)


def replace_once(lines, old, new):
    for i, raw in enumerate(lines):
        if raw.split("|")[0].strip() == old:
            return lines[:i] + [new] + lines[i + 1:]
    raise CheckError(f"selftest anchor not found: {old!r}")


def delete_once(lines, old):
    for i, raw in enumerate(lines):
        if raw.split("|")[0].strip() == old:
            return lines[:i] + lines[i + 1:]
    raise CheckError(f"selftest anchor not found: {old!r}")


def delete_block(lines, block):
    stripped = [raw.split("|")[0].strip() for raw in lines]
    for start in range(len(lines) - len(block) + 1):
        if stripped[start:start + len(block)] == block:
            return lines[:start] + lines[start + len(block):]
    raise CheckError(f"selftest block not found: {block!r}")


def delete_once_after(lines, anchor, target):
    seen = False
    for i, raw in enumerate(lines):
        stripped = raw.split("|")[0].strip()
        if not seen and stripped == anchor:
            seen = True
            continue
        if seen and stripped == target:
            return lines[:i] + lines[i + 1:]
    raise CheckError(f"selftest anchor not found: {anchor!r}->{target!r}")


def selftest():
    production_lines = (root / PRODUCTION).read_text().splitlines()
    qemu_lines = (root / QEMU).read_text().splitlines()

    _, errors = compare(production_lines, qemu_lines)
    if errors:
        print("selftest: the UNMUTATED comparison already fails; "
              "mutation results would be meaningless")
        for error in errors:
            print("  " + error)
        return 1

    failures = 0
    for name, mutate in MUTATIONS:
        try:
            if name == "QEMU re-enable deletion":
                mutated = (production_lines, mutate(qemu_lines))
            else:
                mutated = (mutate(production_lines), qemu_lines)
        except CheckError as exc:
            print(f"selftest {name}: BROKEN SETUP ({exc})")
            failures += 1
            continue
        _, errors = compare(*mutated)
        if errors:
            print(f"selftest {name}: detected "
                  f"({len(errors)} error(s), first: {errors[0]})")
        else:
            print(f"selftest {name}: NOT DETECTED")
            failures += 1

    if failures:
        print(f"selftest: {failures} mutation(s) went undetected")
        return 1
    print("selftest: all mutations detected, unmutated comparison passes")
    return 0


def main():
    if "--selftest" in sys.argv[1:]:
        return selftest()

    production_lines = (root / PRODUCTION).read_text().splitlines()
    qemu_lines = (root / QEMU).read_text().splitlines()
    try:
        report, errors = compare(production_lines, qemu_lines)
    except CheckError as exc:
        print(f"check-context: {exc}")
        return 1
    for line in report:
        print(line)
    if errors:
        for error in errors:
            print("check-context: " + error)
        return 1
    print("Preemptive context switch path: production and QEMU port in sync "
          "(layout/addresses not compared; sanctioned: MFP EOI, switch "
          "threshold symbol)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
