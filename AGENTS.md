# AGENTS.md

## Repository purpose

SSOS-9 is a minimum CLI-only operating system for the Sharp X68000. The only supported scheduler is preemptive, driven by the MC68000 Timer D interrupt. It has a root CLI, heartbeat/idle/reaper kernel tasks, a first-fit arena allocator, an S9M1 image validator/registry, private data fixups, built-in PIC process execution, and an initial `trap #0` syscall ABI for process control and inspection. External module loading, a C module runtime, and file I/O/redirect remain unimplemented. Runtime requirements are an MC68000 at 16 MHz or faster and 12 MiB contiguous main RAM.

## Layout

- `ssos/boot/`: XDF IPL boot loader.
- `ssos/os/`: kernel, preemptive scheduler, arena allocator, module registry, process table, and CLI.
- `ssos/include/ssos9/syscall.h`: syscall numbers and fixed-width request layouts.
- `ssos/standalone/`: Human68K `.x` launcher and return-to-Human68K path.
- `tools/makedisk/`: XDF image builder.
- `tests/`: host and QEMU verification sources.
- `tools/`: build configuration and verification utilities.

## Build

Set up the cross compiler in each shell, then build/install from the repository root:

```sh
source ~/.elf2x68k
make all
make install
make debug
```

`make install` creates `ssos9.x` and `ssos9.xdf` and copies both to `~/tmp`. `make debug` writes the standalone ELF disassembly, symbol table, and link map under `ssos/standalone/build/`. Trailing `&` is parsed independently of whitespace for all commands; background `run` launches a module process, while background builtins run in isolated kernel-entry child processes. IOCS access is serialized by `app/cli_console.c`. The CLI `modules` and `mem` commands expose shared image/reference and arena free/largest-block snapshots. The CLI `debug` command prints tick, task/stack, and MFP status; `trace` prints the last 16 scheduler events. `ssos/config.mk` is the source for scheduler slot/stack counts, the XDF RAM end, and the standalone arena size. Build outputs and intermediates are ignored by Git. There is no scheduler selection variable; builds are preemptive-only.

## Implementation conventions

Use four spaces in C, K&R braces, and `snake_case` symbols. Keep kernel APIs prefixed with `ss_`. Keep this file aligned with the actual source tree and supported build commands. Do not reintroduce cooperative scheduling or graphical/windowing subsystems into the minimum CLI build without an explicit project change.

## Verification boundary

A successful cross-build verifies assembly, compilation, linking, and XDF image construction. The QEMU suite exercises production scheduler, arena, module image/registry, private data/fixups, syscall trap entry/dispatch, and synthetic interrupted-frame code through a QEMU port of the Timer D context-switch path. It does not verify the production MFP handler, IOCS console, Human68K startup, or real X68000 behavior. Do not claim those checks without running them and recording the exact environment.
