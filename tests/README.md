# Scheduler verification

The OS build is preemptive-only and CLI-only. Automated checks exercise the
scheduler, process/module primitives, 68000 assembly samples, and the MC68000
assembler. The root Makefile exposes:

- For SSOS-9 feature changes, normally update and run `make test` and
  `make test-qemu`; add cases to the suite that covers the changed behavior.
- `make test`: native C tests, FAT image tests, mux and editor host tests,
  and the assembler and linker host unit tests (`test-asm-host`,
  `test-link-host`), including FAT16 create/replace interoperability
  with `mkfs.fat`, `fsck.fat`, and `mcopy` when available.
- `make test-asm-host`: assembler host unit tests against fixed expectations
  (lexer/parser: 341 checks, encoder: 266 checks, S9O1 object output). It
  also builds `build/asm-host`, a host entry point sharing the module's
  assembler core, which the golden and QEMU tests reuse.
- `make test-asm-golden`: depends on `test-asm-host` and compares the text
  bytes of the `tests/asm/cases` sources between `asm-host` and
  `m68k-xelf-as`; needs `source ~/.elf2x68k`. See the golden paragraph below.
- `make test-link`: linker host unit build (`test-link-host`) plus the
  linker golden comparison (`test-link-golden`, `tests/link_golden.py`);
  needs `source ~/.elf2x68k`. See the linker paragraph below.
- `make test-qemu`: preemptive scheduler/context-switch tests under
  `qemu-system-m68k`. It cross-builds the editor, assembler, and linker
  S9M1 modules plus `link-host`, runs the assembler host tests first, and
  t13 includes an on-target assembler check.
- `make test-asm`: standalone assembly samples under QEMU plus the assembler
  golden comparison. The sample run mainly checks the examples and the test
  environment; run the golden part (with `test-asm-host`) when changing the
  assembler or its cases.
- `make verify-check`: reports target artifacts affected by the current changes.

The native suite exercises scheduler queue and lifecycle logic with host mocks.
The Human68k SCSI volume path has synthetic host fixtures and QEMU t16.
They exercise partition/BPB validation, 512-byte SCSI blocks combined into
1024-byte sectors, directory lookup, and file creation and reading. Host
tests also cover replacement, incremental writes, both FAT byte orders,
1024-byte physical blocks, and write-error fencing. These fixtures do not
establish compatibility with a real Human68k HDD or XEiJ IOCS.
The FAT16 storage integrity test covers 512- and 1024-byte sectors, 2 GiB sparse
images with external FAT tools, incremental reads/writes, abort and replacement,
rejection of unequal FAT copies and read errors at mount, and fencing later
writes after an injected failure that may already have modified a FAT copy. It
does not model power loss or establish a recovery guarantee.
It also tests the production shell parser: attached/spaced trailing `&`,
whitespace, foreground commands, token limits, and rejection of internal or
repeated `&` before dispatch. Native integration tests exercise background
builtin dispatch with the production process/scheduler/arena code: copied
arguments, child-only exit, wait ownership, deferred kill during output,
resource exhaustion/rollback, and reclaim. Host tests call C task entries
explicitly; they do not switch 68000 stacks or execute the module's machine code.
The QEMU suite drives production scheduler and arena C code and a QEMU port of
the Timer D context-switch path. It covers round-robin/preemption, register
preservation, sleeping with an idle task, task exit and slot reuse, cancellation
of a waiting task, arena split/coalesce, S9M1 range/CRC validation, module
reference/unload behavior, module enumeration without retaining references,
private data initialization/fixups, restoring a
synthetic process SR/PC frame with A3/A5 values, and two processes running the
same PIC counter code with independent data/GOT/stack allocations. Process
integration also tests Ctrl-C killing a foreground process, Ctrl-C cancelling
only a background `wait`, and preserving ordinary keys typed during that wait.
T13 also contains a kernel-entry child argument-copy/return/reap regression.
T13 also loads the generated S9M1 image through a FAT12 disk and a mocked IOCS
2HD reader, exercises the actual CLI and write syscall, waits for Hello to
exit, and checks failure cleanup. The C Hello ELF and disk are built with
`m68k-xelf-gcc`, so source `~/.elf2x68k` before `make test-qemu`.
QEMU does not run the production MFP interrupt handler on X68000 hardware.

The assembler is verified at three levels. `test-asm-host` compiles the
production lexer/parser/encoder/object writer for the host and checks them
against fixed expectations (341 parse checks, 266 encode checks, plus S9O1
object output); it also builds `build/asm-host`, a host entry point sharing
the module's `assemble.c` core. `test-asm-golden` assembles the cases in
`tests/asm/cases` with both `asm-host` and `m68k-xelf-as` and requires
byte-identical `.text` sections: 7 positive cases must match byte for byte,
and the 7 `*.err.s` cases must fail with a `file:line` diagnostic. GAS keeps
0 in a field it relocates against a defined section symbol and stores the
value as an addend, while the S9O1 contract stores the resolved
section-relative value in the field bytes, so the comparison
(`tests/asm_golden.py`) folds GAS-side defined relocations into the bytes
before comparing. Binutils 2.46 m68k GAS turns `$hex` literals into
undefined symbols and rejects forward `.s`/`.b` branches, so positive cases
use `0x` literals and backward short branches only. In QEMU, t13 stores
`tests/asm/qemu-prog.s` on FAT16 as `PROG.S`, spawns the real S9M1 `asm`
module, waits for its exit, and byte-compares the produced `PROG.O` against
the object built by `asm-host` on the host. Like the rest of the QEMU suite,
this runs under the QEMU port of the Timer D context-switch path; it does
not run the assembler on real hardware.

The linker (`apps/link`, contract `docs/linker-contract.md`) is verified at
two host levels plus one on-target level. `test-link-host` builds
`build/link-host`, a host entry point sharing the module core, with the
same `-Wall -Wextra -Werror` profile. `test-link-golden` (`tests/link_golden.py`)
runs two checks: a cross-comparison that links `tests/asm/link/golden/gcross`
with both `link-host` and `m68k-xelf-ld` and requires identical text bytes
(only the profile GNU ld can express: text concatenation, PC16 folding,
constant data, every object's text a multiple of 4), and a self-check where
an independent mini-linker in the script computes the expected S9M1 image
(header fields, code, init data, fixups, CRC) for `tests/asm/link` sample
sources exercising A5 displacement folding, cross-object PC16, and a CODE32
fixup; `badmain.s` must fail with an undefined-symbol diagnostic and leave
no output file. In QEMU, t14 stores the sample sources on FAT16, runs the
real `asm` and `ld` modules, byte-compares `PROG.S9M` against the host-built
golden, spawns the linked program, and checks exit status 27, its
`LNK\n` console output, and the undefined-symbol negative case.
`make test` includes `test-link-host`; the CLI `run` command falls back to
registering `NAME` or `NAME.S9M` from the mounted FAT16 root when the
registry and built-ins do not have it.

For the interactive console smoke test, run `source ~/.elf2x68k` and
`make -C tests/target`. Start the current standalone launcher from Human68k,
then insert `tests/target/build/console.xdf` in drive 1. In SSOS-9, run
`load 1 CONSOLE.S9M`, `mux`, and `run console`. The program clears its screen;
each key except `q` is shown as a four-digit hexadecimal value at (2, 6), and
`q` exits. Check window switching and Ctrl-C separately. This is a manual test;
the build alone does not establish emulator input or display behavior.
`background.xdf` contains `BG.S9M`, a 60-record background output test. Load
it from drive 1, start `run background&`, switch windows during its output,
and return to inspect the originating window.
To prepare a single data floppy usable for launching from Human68k and loading
the modules, pass a disposable FAT12 floppy template, a new output path, the
standalone launcher, and the console module to `prepare_console_image.py`.
Pass `--background tests/target/build/BG.S9M` to include the background test.
The helper preserves the template and adjusts the copied entries for Human68k;
the resulting floppy is intended as a data disk, not an IPL boot disk.

On 2026-11-11, the user ran these tests in the Ctrl-enabled MPX68K test build
with standalone SSOS-9 SHA-256
`97f91098409a6099dcd456f955c5dc1b32b6ceca868032f89b45aefd5841959b`.
The console module (SHA-256
`a99975c53946cbefcd4e342280c66dd72fdfec26b9b91caa694a9ddfc540bac8`)
displayed `KEY: 0061`, retained its display across `~2` and `~1`, and was
interrupted by Ctrl-C. The background module (SHA-256
`622b1f2112e08e4826cc97d7f07e6fcde0be9af8144053f577e27daebb22597f`)
continued producing consecutive records during switching and after scrolling;
the observed final screen showed `BG 36` through `BG 59`. This does not verify
the installed MPX68K release, actual X68000 hardware, or every keyboard layout.
After that emulator run, prefix-followed-by-special-key ordering was corrected
and verified by a native regression test. The resulting standalone SHA-256 is
`67c2c78a6817b1c4726fd8457faa5491acc90e8eac6cfc9d9e6d6d157c8a9785`;
that final binary has not been run on MPX68K.

Cross-build success verifies assembly, compilation, linking, and XDF creation. It does not establish correct MFP/Timer D timing, IOCS console behavior, Human68K return behavior, IPL startup, or real hardware/emulator operation. Record those checks separately with the exact environment and artifact hashes.

On 2026-11-11, XEiJ 0.26.10.08 with MC68000 and 12 MiB RAM booted
Human68k 3.02 from a copy of the SCSI ID 0 HDD. The standalone `ssos9.x`
(SHA-256 `8a5723bf9e7b66c5127bb41aabb5642e59395063a2055cab02cd6eb3311b9ef3`)
was launched from floppy drive 1. On a disposable 32 MiB FAT16 image at SCSI
ID 1, the user observed `smount 1`, `swrite XTEST.TXT hello`, `sread`,
`sreplace XTEST.TXT world`, and another `sread` succeed. After stopping XEiJ,
`mtype` returned `world` and `fsck.fat -n` reported no errors. This does not
verify physical media, all FAT16 geometries, or interrupted writes.

On 2026-11-11, the same XEiJ configuration ran the standalone image with
SHA-256 `2d1dabd7ebb4f77efeb348aedfe338398cc04fd8d6e6f5d535cbee7344f59200`.
Using the XEiJ control pipe, F2 selected window 2 and ran `help`; F3 selected
window 3 and ran `ps`. The user copied the screen text after returning with F2
and F1, confirming the separate output was retained and the original window
was restored. This does not establish physical keyboard or MPX68K F-key behavior.

On 2026-11-12, Codex ran `tools/emulator-smoke.sh` against a running XEiJ
instance outside the sandbox. Six checks passed: Human68k prompt, standalone
startup, `mem`, return to Human68k, and the `dir a:` listing and prompt.
The standalone image SHA-256 was
`4330cc1df45e8b7442baf37b4e8bb9484e4836d8122c34fbb531575dd757aa85`.
The XEiJ version, CPU and RAM configuration were not independently confirmed
for this run; physical hardware was not tested. T5 requires a visible `<dir>`
entry on A: and does not verify all DOS functionality.

On 2026-11-12, `tools/emulator-itest.sh a2` completed five standalone
start/debug/exit cycles in the running XEiJ instance. Each cycle displayed the
startup banner, MFP debug values, termination message, Human68k prompt, and
an A: directory listing after `dir a:`. The sampled screens contained no
`$004F` or error text. The displayed IERB/IMRB/TCDCR/TDDR values were
`3E3E770C`, `3E3E7719`, `3E3E7717`, `3E3E7715`, and `3E3E770B`.
The standalone SHA-256 was
`4330cc1df45e8b7442baf37b4e8bb9484e4836d8122c34fbb531575dd757aa85`.
XEiJ version, CPU and RAM were not independently confirmed for this run.
Screens were sampled rather than continuously monitored; MFP state before and
after execution was not compared, and physical hardware was not tested.

On 2026-11-12, `tools/emulator-itest.sh b1` measured the Timer D tick rate in
the running XEiJ instance. It launched the standalone image, read `ticks=`
from `debug`, waited, read it again, and compared the delta against
200 Hz times the measured wall-clock interval with a 20% tolerance. Three
consecutive runs reported `delta=1790` against `expected~1800` over 9 seconds
(0.6% below nominal). The standalone SHA-256 was
`4330cc1df45e8b7442baf37b4e8bb9484e4836d8122c34fbb531575dd757aa85`.
The measurement samples two instants seconds apart; it does not establish
cycle-accurate MFP timing, the XEiJ load-factor configuration, or real
hardware behavior.

On 2026-11-12, `tools/emulator-itest.sh a3` (also `make test-emulator-a3`)
verified the XDF IPL boot path and the PID 1 exit protection in XEiJ. Because
XEiJ's Java menus are not accessible to UI scripting, the test assigns the
disk by writing `fd0=~/tmp/ssos9.xdf` into `~/XEiJ.ini` and restarting the
emulator app (STD boot order: floppy when inserted, HDD otherwise). It
confirmed the `SSOS-9: minimal X68000 CLI` banner after the IPL boot, that
`exit` prints `Cannot exit the initial SSOS-9 process (PID 1).` and leaves
the prompt alive, and — after removing `fd0` and restarting again — that
Human68k boots back to `A:\>`. Three consecutive runs passed. The XDF
SHA-256 was
`e8bf92ec95c40a26d599a17a224a1210e66fcf05a18774b06f2466a5730aac5f`.
This exercises the emulator's floppy boot path through the ini assignment,
not XEiJ's FD menu, physical media, or real hardware.

The A3 script now backs up the entire `~/XEiJ.ini` before stopping XEiJ and
restores it before restarting XEiJ, including on errors and HUP, INT, or TERM.
If XEiJ cannot be stopped or the restore fails, the backup is retained and its
path is reported. SIGKILL or a system failure cannot run shell cleanup.

On 2026-11-12, `make test-emulator EMULATOR_TESTS=e3` passed twice against a
running XEiJ instance. The final run showed `Hello`, `PID 2 exited with status
0`, and the subsequent SSOS-9 prompt. The `trace` output contained Timer D
switches (for example, `task=1 other=3 value=00000002`), and `exit` returned
to the Human68k `A:\>` prompt. The standalone SHA-256 was
`4330cc1df45e8b7442baf37b4e8bb9484e4836d8122c34fbb531575dd757aa85`.
The running emulator's exact version, CPU and RAM were not independently
confirmed. The trace confirms timer-driven switches in the captured interval,
not necessarily during the short hello process. Physical hardware was not tested.

On 2026-11-13, `tools/emulator-itest.sh d1` verified the Human68k-format SCSI
HDD create/read/replace path against the real A: HDD in XEiJ. The script
copies a unique fixture from F: into `A:\TEST` with Human68k, boots the
standalone image, identifies A: by scanning `smount 0..7` read-only until
`sread` returns the fixture content, then writes `swrite/sread/sreplace/sread`
on a unique new name and verifies the result from Human68k after a guest
reset. A: was identified as SCSI ID 0. The run passed with standalone
SHA-256
`6c6cf6c5cd7389d337e3a02cb224aa22d87c02b4dfff50c84e419e6e31cc7058`.
The verification needed two fixes that only real media expose: SSOS-9's path
lookup now compares 8.3 names case-insensitively (Human68k `copy` stores
lowercase names, matching its own case-insensitive DOS semantics), and the
script resets the guest after `exit` because Human68k's track-buffer disk
cache hides directory entries written behind it (the pre-reset `dir` showed
an SSOS-9-created file only after a reset). The Human68k-side checks are
screen-text based, so 18.3 names, Shift-JIS names, and subdirectory changes
inside `A:\TEST` are not compared. Physical hardware was not tested.

Operational notes from the same session: XEiJ's `typekey yen` delivers no
key press to the guest (presskey, a 0.15 s hold, releasekey works and is
what the script uses for `\`); after many in-guest SSOS-9 start/exit cycles
the guest once stopped echoing keys entirely and a full XEiJ restart
(restart the app, not just the guest) restored input, after which the same
binary worked unchanged.

On 2026-11-13, `tools/emulator-itest.sh` gained B2-B4, C1-C5, and D2-D4
areas. B2 (trace: 16 scheduler events with non-empty kinds while the builtin
counter ran), B3 (ticks advanced by 1860 over ~9 s with the spinner running
while typing/debug/ps stayed responsive; the counter's own count is not
screen-observable, so responsiveness plus tick advance is the check), B4
(real-key Ctrl-C on foreground `run counter`: `^C`, `PID 2 interrupted`,
prompt back, arena exactly `free_bytes=262132 largest_free=262132` again),
C1 (help output equals the cli.c table after whitespace normalization), C2
(8x `ps` kept the prompt on the last console row with the function-key row
last; the never-written bottom-right cell is not text-copy distinguishable
and stays a manual check), and C3 (F1/F2/F3 switching retained and restored
each window's output) all passed against standalone SHA-256
`6c6cf6c5cd7389d337e3a02cb224aa22d87c02b4dfff50c84e419e6e31cc7058`.
C4 ran and failed by design on this environment: IOCS encodes ctrl only for
letter keys (ctrl-c works; ctrl+] never arrives as 0x1d) and shift+caret
delivers the JIS full-width tilde pair instead of ASCII 0x7e, so neither mux
prefix is deliverable through the emulated keyboard; the script documents
this, verifies the stray input harmlessly errors, and exits via F1. F-key
switching itself is covered by C3. XEiJ `typekey &` also drops the
ampersand, so the script sends it as shift+6. C5 and D2-D4 are implemented
but not yet executed. Physical hardware was not tested.

On 2026-11-13, `tools/emulator-itest.sh` gained the F1, F2, and G1 areas,
all passing against the same standalone SHA-256
`6c6cf6c5cd7389d337e3a02cb224aa22d87c02b4dfff50c84e419e6e31cc7058`.
F1: `help&`/`ps&`/`mem&` kernel-entry children interleaved on one console
with all three outputs line-intact, `wait PID` reaped each, and the arena
returned to its exact baseline. F2: since the builtin counter emits nothing,
the test places `tests/target/build/BG.S9M` in `A:\TEST` (first copy kept,
never overwritten) and runs `run test/bg&` under the multiplexer; BG record
numbers advanced across three F2/F1 round trips (12->34->54->59) while
window 1 was hidden and the module finished with status 0. G1: five
spawn/kill/wait cycles reaped PIDs 2..6 and the arena matched the boot
baseline exactly. The CLI's `wait` requires a PID argument (no bare wait),
which the tests follow. Two operational notes: `place_in_test` failed on
its first execution because zsh expands later assignments in the same
`local` statement before earlier ones take effect under `set -u` (fixed by
splitting the declaration), and System Events transiently refused to
activate the java process once (-10006); a manual retry cleared it. This
run wrote only the immutable `BG.S9M` into `A:\TEST`. Physical hardware
was not tested.
