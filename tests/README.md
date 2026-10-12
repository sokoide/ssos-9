# Scheduler verification

The OS build is preemptive-only and CLI-only. Automated checks are limited to
the scheduler and 68000 assembly samples. The root Makefile exposes:

- `make test`: native C tests.
- `make test-qemu`: preemptive scheduler/context-switch tests under `qemu-system-m68k`.
- `make test-asm`: standalone assembly samples under QEMU.
- `make verify-check`: reports target artifacts affected by the current changes.

The native suite exercises scheduler queue and lifecycle logic with host mocks.
The QEMU scheduler suite drives production scheduler C code and a QEMU port of
the Timer D context-switch path. It does not run the production MFP interrupt
handler on X68000 hardware.

Cross-build success verifies assembly, compilation, linking, and XDF creation. It does not establish correct MFP/Timer D timing, IOCS console behavior, Human68K return behavior, IPL startup, or real hardware/emulator operation. Record those checks separately with the exact environment and artifact hashes.
