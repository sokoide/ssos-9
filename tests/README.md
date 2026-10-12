# Scheduler verification

The OS build is preemptive-only and CLI-only. Automated checks exercise the
scheduler, process/module primitives, and 68000 assembly samples. The root Makefile exposes:

- `make test`: native C tests.
- `make test-qemu`: preemptive scheduler/context-switch tests under `qemu-system-m68k`.
- `make test-asm`: standalone assembly samples under QEMU.
- `make verify-check`: reports target artifacts affected by the current changes.

The native suite exercises scheduler queue and lifecycle logic with host mocks.
The QEMU suite drives production scheduler and arena C code and a QEMU port of
the Timer D context-switch path. It covers round-robin/preemption, register
preservation, sleeping with an idle task, task exit and slot reuse, cancellation
of a waiting task, arena split/coalesce, S9M1 range/CRC validation, module
reference/unload behavior, private data initialization/fixups, and restoring a
synthetic process SR/PC frame with A3/A5 values, and two processes running the
same PIC counter code with independent data/GOT/stack allocations. QEMU does not run the
production MFP interrupt handler on X68000 hardware.

Cross-build success verifies assembly, compilation, linking, and XDF creation. It does not establish correct MFP/Timer D timing, IOCS console behavior, Human68K return behavior, IPL startup, or real hardware/emulator operation. Record those checks separately with the exact environment and artifact hashes.
