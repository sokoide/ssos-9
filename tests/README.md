# Scheduler verification

The OS build is preemptive-only and CLI-only. Automated checks exercise the
scheduler, process/module primitives, and 68000 assembly samples. The root Makefile exposes:

- For SSOS-9 feature changes, normally update and run `make test` and
  `make test-qemu`; add cases to the suite that covers the changed behavior.
- `make test`: native C tests and FAT image tests, including FAT16 create/replace
  interoperability with `mkfs.fat`, `fsck.fat`, and `mcopy` when available.
- `make test-qemu`: preemptive scheduler/context-switch tests under `qemu-system-m68k`.
- `make test-asm`: standalone assembly samples under QEMU, mainly for checking
  the examples and test environment. Run it when changing those samples or
  their execution environment; it is not normally part of an OS feature change.
- `make verify-check`: reports target artifacts affected by the current changes.

The native suite exercises scheduler queue and lifecycle logic with host mocks.
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
