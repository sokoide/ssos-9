# AGENTS.md

## Repository purpose

SSOS-9 is a minimum CLI-only operating system for the Sharp X68000. The only supported scheduler is preemptive, driven by the MC68000 Timer D interrupt. It has a root CLI, heartbeat/idle/reaper kernel tasks, a first-fit arena allocator, an S9M1 image validator/registry, private data fixups, PIC process execution, and an initial `trap #0` syscall ABI. A restricted C Hello module, read-only 2HD FAT12 loader, limited Human68k-format SCSI HDD file operations with incremental file descriptors, a three-console shell multiplexer, an S9M1 vi-style editor, an MC68000 assembler producing S9O1 objects, and an S9O1 linker producing S9M1 executables are available; CLI `run` falls back to registering a module from the mounted volume when the registry and built-ins do not have it. A general C runtime and redirection remain unimplemented. The Human68k HDD path has only synthetic host/QEMU coverage; disk and IOCS behavior need emulator and hardware verification. Runtime requirements are an MC68000 at 16 MHz or faster and 12 MiB contiguous main RAM.

## Layout

- `ssos/boot/`: XDF IPL boot loader.
- `ssos/os/`: kernel, preemptive scheduler, arena allocator, module registry, process table, and CLI.
- `ssos/include/ssos9/syscall.h`: syscall numbers and fixed-width request layouts.
- `ssos/standalone/`: Human68K `.x` launcher and return-to-Human68K path.
- `apps/`: S9M1 application modules (vi-style editor in `apps/edit/`, MC68000 assembler in `apps/asm/`, S9O1 linker in `apps/link/`).
- `tools/makedisk/`: XDF image builder.
- `tests/`: host and QEMU verification sources, including assembler golden cases under `tests/asm/cases/`, linker sample and golden sources under `tests/asm/link/`, and `tests/link_golden.py`.
- `tools/`: build configuration and verification utilities; `s9ob.py` validates and dumps S9O1 objects.

## Build

Set up the cross compiler in each shell, then build/install from the repository root:

```sh
source ~/.elf2x68k
make all
make install
make debug
```

`make install` creates `ssos9.x` and `ssos9.xdf` and copies both to `~/tmp`. `make debug` writes the standalone ELF disassembly, symbol table, and link map under `ssos/standalone/build/`. The root `make all` also builds the editor, assembler, and linker S9M1 modules under `apps/`; `apps/asm` builds with `-Os` under a 32768-byte `.text` limit enforced by `tools/mkmodule.py`. `make test`, `make test-asm`, `make test-link`, and `make test-qemu` run the host, assembler golden/sample, linker golden, and QEMU suites under `tests/`; `make verify` runs the cross-build and all suites. `make test-emulator` runs XEiJ integration tests (`tools/emulator-itest.sh`) against a running emulator: `EMULATOR_TESTS` selects 26 IDs from `make test-emulator-list`, defaulting to the 16-test core that never writes the real HDD (a3 restarts XEiJ and transiently edits `~/XEiJ.ini`, restoring both; media-writing and staged-fixture tests are opt-in, as is the new c6 line-editing check). The floppy load regression H2 runs on MPX68K because XEiJ media swapping is unreliable; since 2026-11-18 `/Applications/MPX68K.app` is a local unsigned fork build (`oss/MPX68K` @ `4fc9e85`, includes MonitorSocket `7f3b4d9`) and H2 is automatable: send `PAUSE`, `MOUNTFDD <drive 0-1> <image>`, `EJECTFDD <drive>`, `RESUME` lines to the `~/mpx68k_monitor.sock` Unix socket, type guest keys with `osascript keystroke` after running `keywitch -r` in the guest, and read the screen with `screencapture -R`. `tools/mpx68k-h2.sh` (`make test-mpx68k-h2`) packages the whole H2 procedure — dedicated-image creation, an eleven-gate flow including the 3-cluster `load 1 edit.s9m` check and the missing-file/no-media load error paths, per-gate verdicts by a Swift + Vision OCR helper over captured screens, and EJECTFDD/SHA-256/`fsck.fat -n` cleanup on every exit (exit 2 = OCR unavailable, 3 = environment); it has not yet been executed against a live MPX68K. Its prefs live at `~/Library/Preferences/NANKIN.X68000.plist` (`monitorSocketEnabled`, `AutoMountMode_v1=lastSession` restores the saved FD0 Human68k disk); `defaults <bundle-id>` resolves to the sandbox container, so edit that plist by explicit path. Prepare the H2 floppy with `tests/target/prepare_console_image.py` (its repeatable `--module` option adds extra S9M1 modules such as `EDIT.S9M` for the multi-cluster gate) because plain `build/console.xdf` carries only `CONSOLE.S9M` without the `SS9CON.X` launcher; in this setup Human68k boots from FD0 (`A:`) while the FD1 media appears as `C:`. These targets are excluded from `make verify` because they need a live emulator, permissions, and guest state. See `tests/README.md` for coverage and H2 procedure. Mux selection uses F1/F2/F3 or Ctrl-] followed by 1..3; the screen banner lists those controls. The CLI line editor accepts BS, the cursor keys, and DEL (IOCS scan codes 0x3B/0x3D left/right, 0x37 forward delete at the cursor, measured on XEiJ 2026-11-21); other special keys are ignored. Trailing `&` is parsed independently of whitespace for all commands; background `run` launches a module process, while background builtins run in isolated kernel-entry child processes. IOCS access is serialized by `app/cli_console.c`. The CLI `modules` and `mem` commands expose shared image/reference and arena free/largest-block snapshots. The CLI `debug` command prints tick, task/stack, and MFP status; `trace` prints the last 64 scheduler events. `ssos/config.mk` is the source for scheduler slot/stack counts, the XDF RAM end, and the standalone arena size. Build outputs and intermediates are ignored by Git. There is no scheduler selection variable; builds are preemptive-only.

## Implementation conventions

Use four spaces in C, K&R braces, and `snake_case` symbols. Keep kernel APIs prefixed with `ss_`. Keep this file aligned with the actual source tree and supported build commands. Do not reintroduce cooperative scheduling or graphical/windowing subsystems into the minimum CLI build without an explicit project change.

## Verification boundary

A successful cross-build verifies assembly, compilation, linking, and XDF image construction. The QEMU suite exercises production scheduler, arena, module image/registry, private data/fixups, syscall trap entry/dispatch, and synthetic interrupted-frame code through a QEMU port of the Timer D context-switch path. It also spawns the real S9M1 assembler module on target (t13) and byte-compares the S9O1 output with the host build of the same core, and assembler encoding is compared byte-for-byte against `m68k-xelf-as` in the host golden tests. The linker is verified by host golden tests that byte-compare the S9M1 image with an independent mini-linker and, for a restricted profile, with `m68k-xelf-ld`, and on target by t14 (real `asm`/`ld` modules, spawn of the linked program, and the CLI `run` FAT16 fallback) and t15 (edit → asm → ld → run across mux windows). It does not verify the production MFP handler, IOCS console, Human68K startup, or real X68000 behavior. Do not claim those checks without running them and recording the exact environment.

For emulator-assisted verification beyond QEMU, see `docs/emulator-remote-operation.md`: XEiJ exposes `~/tmp` to the guest as F: (HFS host file system), its control pipe (`pastepipe=on` in `~/XEiJ.ini`) sends keys, Option-Shift-C copies the text screen so `pbpaste` can read it, and FAT16 result images under `~/tmp` are inspected offline after shutdown. Emulator runs never substitute for the real-hardware boundary above; record the environment and artifact hashes separately.

Acceptance-run findings (2026-11-18): the XEiJ runner works on observed frames and can abort transiently — `run_c2` now waits for the prompt to settle on the last console row because a mid-burst frame raced the text copy, and one full 25-case run aborted with a one-off screen-copy environment error. In both cases the ini and original media hashes stayed equal, so re-running on the same disposable profile was valid; record every attempt and compare hashes before re-running.

For Codex, run `tools/emulator-smoke.sh` outside the sandbox when the sandbox blocks process enumeration or macOS automation. In the verified environment, XEiJ was already running as `java`, Ghostty had permission to control System Events and the device, and the script used `${TMPDIR}/XEiJControl`. See the emulator operation document for prerequisites, observed errors, and verification limits.
