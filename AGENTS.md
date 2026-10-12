# AGENTS.md

## Repository purpose

SSOS-9 is a minimum CLI-only operating system for the Sharp X68000. The only supported scheduler is preemptive, driven by the MC68000 Timer D interrupt. The current system has one CLI process and kernel worker tasks; it does not yet implement OS-9-style processes, module loading, PIC modules, or process-private data areas.

## Layout

- `ssos/boot/`: XDF IPL boot loader.
- `ssos/os/`: kernel, preemptive scheduler, and CLI.
- `ssos/standalone/`: Human68K `.x` launcher and return-to-Human68K path.
- `tools/makedisk/`: XDF image builder.
- `tests/`: host and QEMU verification sources.
- `tools/`: build configuration and verification utilities.

## Build

Set up the cross compiler in each shell, then build/install from the repository root:

```sh
source ~/.elf2x68k
make
make install
```

`make install` creates `ssos9.x` and `ssos9.xdf` and copies both to `~/tmp`. Build outputs and intermediates are ignored by Git. There is no scheduler selection variable; builds are preemptive-only.

## Implementation conventions

Use four spaces in C, K&R braces, and `snake_case` symbols. Keep kernel APIs prefixed with `ss_`. Keep this file aligned with the actual source tree and supported build commands. Do not reintroduce cooperative scheduling or graphical/windowing subsystems into the minimum CLI build without an explicit project change.

## Verification boundary

A successful cross-build verifies assembly, compilation, linking, and XDF image construction. It does not verify MFP/Timer D behavior, IOCS console behavior, booting under Human68K, or real hardware/emulator execution. Do not claim those checks without running them and recording the exact environment.
