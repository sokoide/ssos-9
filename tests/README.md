# Verification

The supported OS build is preemptive-only. The root Makefile exposes:

- `make test`: native C tests.
- `make test-qemu`: preemptive scheduler/context-switch tests under `qemu-system-m68k`.
- `make test-asm`: standalone assembly samples under QEMU.
- `make verify-check`: reports target artifacts affected by the current changes.

The QEMU scheduler suite drives the production scheduler C code and a QEMU port of the Timer D context-switch path. It does not run the production MFP interrupt handler on X68000 hardware.

Cross-build success verifies assembly, compilation, linking, and XDF creation. It does not establish correct MFP/Timer D timing, IOCS console behavior, Human68K return behavior, IPL startup, or real hardware/emulator operation. Record those checks separately with the exact environment and artifact hashes.
