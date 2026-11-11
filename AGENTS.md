# AGENTS.md

## Repository purpose

SSOS-9 is a minimum CLI-only operating system for the Sharp X68000. The only supported scheduler is preemptive, driven by the MC68000 Timer D interrupt. It has a root CLI, heartbeat/idle/reaper kernel tasks, a first-fit arena allocator, an S9M1 image validator/registry, private data fixups, PIC process execution, and an initial `trap #0` syscall ABI. A restricted C Hello module, read-only 2HD FAT12 loader, limited SCSI FAT16 file operations with incremental file descriptors, a three-console shell multiplexer, an S9M1 vi-style editor, and an MC68000 assembler producing S9O1 objects are available. A general C runtime, an S9O1 linker, and redirection remain unimplemented. Disk and IOCS behavior need hardware verification. Runtime requirements are an MC68000 at 16 MHz or faster and 12 MiB contiguous main RAM.

## Layout

- `ssos/boot/`: XDF IPL boot loader.
- `ssos/os/`: kernel, preemptive scheduler, arena allocator, module registry, process table, and CLI.
- `ssos/include/ssos9/syscall.h`: syscall numbers and fixed-width request layouts.
- `ssos/standalone/`: Human68K `.x` launcher and return-to-Human68K path.
- `apps/`: S9M1 application modules (vi-style editor in `apps/edit/`, MC68000 assembler in `apps/asm/`).
- `tools/makedisk/`: XDF image builder.
- `tests/`: host and QEMU verification sources, including assembler golden cases under `tests/asm/cases/`.
- `tools/`: build configuration and verification utilities; `s9ob.py` validates and dumps S9O1 objects.

## Build

Set up the cross compiler in each shell, then build/install from the repository root:

```sh
source ~/.elf2x68k
make all
make install
make debug
```

`make install` creates `ssos9.x` and `ssos9.xdf` and copies both to `~/tmp`. `make debug` writes the standalone ELF disassembly, symbol table, and link map under `ssos/standalone/build/`. The root `make all` also builds the editor and assembler S9M1 modules under `apps/`; `apps/asm` builds with `-Os` under a 32768-byte `.text` limit enforced by `tools/mkmodule.py`. `make test`, `make test-asm`, and `make test-qemu` run the host, assembler golden/sample, and QEMU suites under `tests/`; `make verify` runs the cross-build and all suites. See `tests/README.md` for what each suite covers. Trailing `&` is parsed independently of whitespace for all commands; background `run` launches a module process, while background builtins run in isolated kernel-entry child processes. IOCS access is serialized by `app/cli_console.c`. The CLI `modules` and `mem` commands expose shared image/reference and arena free/largest-block snapshots. The CLI `debug` command prints tick, task/stack, and MFP status; `trace` prints the last 16 scheduler events. `ssos/config.mk` is the source for scheduler slot/stack counts, the XDF RAM end, and the standalone arena size. Build outputs and intermediates are ignored by Git. There is no scheduler selection variable; builds are preemptive-only.

## Implementation conventions

Use four spaces in C, K&R braces, and `snake_case` symbols. Keep kernel APIs prefixed with `ss_`. Keep this file aligned with the actual source tree and supported build commands. Do not reintroduce cooperative scheduling or graphical/windowing subsystems into the minimum CLI build without an explicit project change.

## Verification boundary

A successful cross-build verifies assembly, compilation, linking, and XDF image construction. The QEMU suite exercises production scheduler, arena, module image/registry, private data/fixups, syscall trap entry/dispatch, and synthetic interrupted-frame code through a QEMU port of the Timer D context-switch path. It also spawns the real S9M1 assembler module on target (t13) and byte-compares the S9O1 output with the host build of the same core, and assembler encoding is compared byte-for-byte against `m68k-xelf-as` in the host golden tests. It does not verify the production MFP handler, IOCS console, Human68K startup, or real X68000 behavior. Do not claim those checks without running them and recording the exact environment.
