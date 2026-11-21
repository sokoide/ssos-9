# QEMU exercises the preemptive scheduler and its context-switch path.
CC = m68k-elf-gcc
include ../../../ssos/config.mk
ASFLAGS = -m68000 -Wa,--register-prefix-optional -I../../../ssos/os/kernel
CFLAGS = -m68000 -O2 -g -ffreestanding -nostdlib -fno-builtin -MMD -MP \
	-Wall -Wextra -Wno-unused-parameter -I../common \
	-I../common/include -I../../../ssos/os/kernel -I../../../ssos/os/mem \
	-I../../../ssos/os/module -I../../../ssos/os/process \
	-I../../../ssos/include \
	-DSS_QEMU_TEST \
	-DSS_MAX_TASKS=$(SS_MAX_TASKS) -DSS_TASK_STACK=$(SS_TASK_STACK) \
	-DSS_X_ARENA_BYTES=$(SS_X_ARENA_BYTES)
LDFLAGS = -T ../common/linker.ld -nostdlib -Wl,--gc-sections -lgcc
QEMU = qemu-system-m68k
QFLAGS = -M virt -cpu m68000 -nographic
# Default per-test timeout. Tests park after their verdict, so a green run
# costs the full timeout. Heavy tests override per test below; slower hosts
# can raise any of these (e.g. `make TIMEOUT_SECS=10 run`).
TIMEOUT_SECS ?= 2
t13_process_spawn_TIMEOUT_SECS = 6
t14_link_run_TIMEOUT_SECS = 6
t15_mux_flow_TIMEOUT_SECS = 10
t17_human68k_cli_mount_TIMEOUT_SECS = 6

SSOS = ../../../ssos/os
TESTS = t01_round_robin t02_register_save t03_sleep_wakeup \
	t04_main_task_register t05_timerd_cadence t06_idle_sleep t07_task_reap \
	t08_cancel_waiting t09_arena t10_module_image t11_module_registry \
	t12_interrupted_start t13_process_spawn t14_link_run t15_mux_flow t16_human68k_volume \
	t17_human68k_cli_mount
COMMON_OBJS = stub.o sched.o syscall.o syscall_entry.o wakeups.o main_task.o arena.o image.o module.o builtin.o process.o \
	system_arena_local.o preempt_ctx_switch.o
# COMMON_OBJS with the threshold-1 Timer D port (switch every tick) for the
# fast-switching tests below; the default object matches production (10).
FAST_OBJS = stub.o sched.o syscall.o syscall_entry.o wakeups.o main_task.o arena.o image.o module.o builtin.o process.o \
	system_arena_local.o preempt_ctx_switch_1.o

stub.o: ../common/stub.c $(SSOS)/kernel/scheduler.h
	$(CC) $(CFLAGS) -c $< -o $@
sched.o: $(SSOS)/kernel/scheduler.c
	$(CC) $(CFLAGS) -c $< -o $@
syscall.o: $(SSOS)/kernel/syscall.c
	$(CC) $(CFLAGS) -c $< -o $@
syscall_entry.o: $(SSOS)/kernel/syscall_entry.s
	$(CC) $(ASFLAGS) -c $< -o $@
wakeups.o: $(SSOS)/kernel/preemptive/wakeups.c
	$(CC) $(CFLAGS) -c $< -o $@
main_task.o: $(SSOS)/kernel/main_task.c
	$(CC) $(CFLAGS) -c $< -o $@
arena.o: $(SSOS)/mem/arena.c $(SSOS)/mem/arena.h
	$(CC) $(CFLAGS) -I$(SSOS)/mem -c $< -o $@
image.o: $(SSOS)/module/image.c $(SSOS)/module/image.h
	$(CC) $(CFLAGS) -I$(SSOS)/module -c $< -o $@
module.o: $(SSOS)/module/module.c $(SSOS)/module/module.h $(SSOS)/module/image.h
	$(CC) $(CFLAGS) -I$(SSOS)/module -I$(SSOS)/mem -c $< -o $@
builtin.o: $(SSOS)/module/builtin.c $(SSOS)/module/builtin.h $(SSOS)/module/module.h ../../../examples/hello/build/hello.s9m
	$(CC) $(CFLAGS) -I$(SSOS)/module -c $< -o $@
process.o: $(SSOS)/process/process.c $(SSOS)/process/process.h
	$(CC) $(CFLAGS) -I$(SSOS)/module -I$(SSOS)/mem -I$(SSOS)/process -c $< -o $@
system_arena_local.o: $(SSOS)/mem/system_arena.c $(SSOS)/mem/system_arena.h
	$(CC) $(CFLAGS) -DLOCAL_MODE -I$(SSOS)/mem -c $< -o $@
preempt_ctx_switch.o: preempt_ctx_switch.s $(SSOS)/kernel/scheduler_abi.h $(SSOS)/kernel/context_frame.h
	$(CC) $(ASFLAGS) -x assembler-with-cpp -c $< -o $@
preempt_ctx_switch_1.o: preempt_ctx_switch.s $(SSOS)/kernel/scheduler_abi.h $(SSOS)/kernel/context_frame.h
	$(CC) $(ASFLAGS) -Wa,--defsym,SS_TIMERD_SWITCH_TICKS=1 -x assembler-with-cpp -c $< -o $@
t12_interrupted_start_entry.o: t12_interrupted_start.s
	$(CC) $(ASFLAGS) -c $< -o $@
t13_test_vectors.o: t13_test_vectors.s
	$(CC) $(ASFLAGS) -c $< -o $@
t13_ctrl_c.o: ../../../ssos/os/kernel/ctrl_c.s
	$(CC) $(ASFLAGS) -c $< -o $@
t13_cli_console.o: $(SSOS)/app/cli_console.c $(SSOS)/app/cli_console.h
	$(CC) $(CFLAGS) -I$(SSOS)/app -c $< -o $@
t13_mux.o: $(SSOS)/app/mux.c $(SSOS)/app/mux.h
	$(CC) $(CFLAGS) -I$(SSOS)/app -c $< -o $@
t13_cli_wait.o: $(SSOS)/app/cli_wait.c $(SSOS)/app/cli_wait.h
	$(CC) $(CFLAGS) -I$(SSOS)/app -c $< -o $@
t13_cli.o: $(SSOS)/app/cli.c
	$(CC) $(CFLAGS) -I$(SSOS)/app -c $< -o $@
t13_cli_parse.o: $(SSOS)/app/cli_parse.c
	$(CC) $(CFLAGS) -I$(SSOS)/app -c $< -o $@
t13_fat12.o: $(SSOS)/fs/fat12.c $(SSOS)/fs/fat12.h
	$(CC) $(CFLAGS) -c $< -o $@
t13_fat16_write.o: $(SSOS)/fs/fat16_write.c $(SSOS)/fs/fat12.h
	$(CC) $(CFLAGS) -c $< -o $@
t13_storage.o: $(SSOS)/fs/storage.c $(SSOS)/fs/storage.h
	$(CC) $(CFLAGS) -c $< -o $@
t13_storage_system.o: $(SSOS)/fs/storage_system.c $(SSOS)/fs/storage.h
	$(CC) $(CFLAGS) -c $< -o $@
t13_xdf.o: $(SSOS)/fs/xdf.c $(SSOS)/fs/xdf.h
	$(CC) $(CFLAGS) -c $< -o $@
t16_human68k_volume_impl.o: $(SSOS)/fs/human68k_volume.c $(SSOS)/fs/human68k_volume.h
	$(CC) $(CFLAGS) -c $< -o $@
t16_scsi.o: $(SSOS)/fs/scsi.c
	$(CC) $(CFLAGS) -c $< -o $@
t16_scsi_volume.o: $(SSOS)/fs/scsi_volume.c
	$(CC) $(CFLAGS) -c $< -o $@
# scsi_iocs.c needs the real toolchain <x68k/iocs.h> (struct iocs_readcap
# and the _iocs_s_* prototypes), not the QEMU stub, so the toolchain include
# path goes first for this object only; the _iocs_s_* symbols resolve to the
# mocks inside t17_human68k_cli_mount.o at link time.
t17_scsi_iocs.o: $(SSOS)/fs/scsi_iocs.c
	$(CC) -I$(XELF_BASE)/m68k-elf/include $(CFLAGS) -c $< -o $@
../../../examples/hello/build/hello.s9m: FORCE
	$(MAKE) -C ../../../examples/hello all
../../../examples/hello/build/hello-data.xdf: ../../../examples/hello/build/hello.s9m
	$(MAKE) -C ../../../examples/hello all
../../../apps/edit/build/edit.s9m: FORCE
	$(MAKE) -C ../../../apps/edit all
../../../apps/link/build/ld.s9m: FORCE
	$(MAKE) -C ../../../apps/link all
../../../tests/build/link-host: FORCE
	$(MAKE) -C ../../../tests test-link-host
hello_image.o: hello_image.s ../../../examples/hello/build/hello.s9m ../../../examples/hello/build/hello-data.xdf ../../../apps/edit/build/edit.s9m ../../../apps/asm/build/asm.s9m ../../../apps/link/build/ld.s9m ../../../tests/asm/qemu-prog.s build/asm-expected.bin ../../../tests/asm/link/start.s ../../../tests/asm/link/main.s ../../../tests/asm/link/obj.s ../../../tests/asm/link/badmain.s build/link-expected.bin
	$(CC) $(ASFLAGS) -c $< -o $@

# Expected S9O1 object for the on-target assembler test, produced by the
# host build of the same assembler core (tests/Makefile test-asm-host).
build/asm-expected.bin: ../../../tests/asm/qemu-prog.s ../../../tests/build/asm-host
	@mkdir -p build
	../../../tests/build/asm-host $< $@
# Expected S9M1 image for the on-target linker test: the three link-sample
# objects assembled by the host assembler core and linked by the host
# linker core (tests/Makefile test-link-host), byte-identical to the
# on-target `ld` run.
build/link-expected.bin: ../../../tests/asm/link/start.s ../../../tests/asm/link/main.s ../../../tests/asm/link/obj.s ../../../tests/build/asm-host ../../../tests/build/link-host
	@mkdir -p build/golden
	../../../tests/build/asm-host ../../../tests/asm/link/start.s build/link-start.o
	../../../tests/build/asm-host ../../../tests/asm/link/main.s build/link-main.o
	../../../tests/build/asm-host ../../../tests/asm/link/obj.s build/link-obj.o
	@# The image stores the module name taken from the output base name,
	@# so link into PROG.S9M to match the on-target `ld PROG.S9M ...` run.
	../../../tests/build/link-host build/golden/PROG.S9M build/link-start.o build/link-main.o build/link-obj.o
	mv build/golden/PROG.S9M $@
t02_register_save_entry.o: t02_register_save.s
	$(CC) $(ASFLAGS) -c $< -o $@

-include *.d

.PHONY: all run run-one clean
all: $(addsuffix .elf,$(TESTS))

%.elf: %.o $(COMMON_OBJS) ../common/linker.ld
	$(CC) $(CFLAGS) $< $(COMMON_OBJS) $(LDFLAGS) -o $@

t02_register_save.elf: t02_register_save.o t02_register_save_entry.o $(COMMON_OBJS) ../common/linker.ld
	$(CC) $(CFLAGS) $< t02_register_save_entry.o $(COMMON_OBJS) $(LDFLAGS) -o $@

# t05 asserts the production 10-tick cadence, so it links the default object.
t05_timerd_cadence.elf: t05_timerd_cadence.o stub.o sched.o wakeups.o main_task.o preempt_ctx_switch.o ../common/linker.ld
	$(CC) $(CFLAGS) $< stub.o sched.o wakeups.o main_task.o preempt_ctx_switch.o $(LDFLAGS) -o $@

t12_interrupted_start.elf: t12_interrupted_start.o t12_interrupted_start_entry.o $(FAST_OBJS) ../common/linker.ld
	$(CC) $(CFLAGS) t12_interrupted_start.o t12_interrupted_start_entry.o $(FAST_OBJS) $(LDFLAGS) -o $@

t13_process_spawn.elf: t13_process_spawn.o t13_test_vectors.o t13_ctrl_c.o t13_cli_wait.o t13_cli_console.o t13_mux.o t13_cli.o t13_cli_parse.o t13_fat12.o t13_fat16_write.o t13_storage.o t13_storage_system.o t13_xdf.o hello_image.o $(COMMON_OBJS) ../common/linker.ld
	$(CC) $(CFLAGS) t13_process_spawn.o t13_test_vectors.o t13_ctrl_c.o t13_cli_wait.o t13_cli_console.o t13_mux.o t13_cli.o t13_cli_parse.o t13_fat12.o t13_fat16_write.o t13_storage.o t13_storage_system.o t13_xdf.o hello_image.o $(COMMON_OBJS) $(LDFLAGS) -o $@

t14_link_run.elf: t14_link_run.o t13_test_vectors.o t13_ctrl_c.o t13_cli_wait.o t13_cli_console.o t13_mux.o t13_cli.o t13_cli_parse.o t13_fat12.o t13_fat16_write.o t13_storage.o t13_storage_system.o t13_xdf.o hello_image.o $(COMMON_OBJS) ../common/linker.ld
	$(CC) $(CFLAGS) t14_link_run.o t13_test_vectors.o t13_ctrl_c.o t13_cli_wait.o t13_cli_console.o t13_mux.o t13_cli.o t13_cli_parse.o t13_fat12.o t13_fat16_write.o t13_storage.o t13_storage_system.o t13_xdf.o hello_image.o $(COMMON_OBJS) $(LDFLAGS) -o $@

t15_mux_flow.elf: t15_mux_flow.o t13_test_vectors.o t13_ctrl_c.o t13_cli_wait.o t13_cli_console.o t13_mux.o t13_cli.o t13_cli_parse.o t13_fat12.o t13_fat16_write.o t13_storage.o t13_storage_system.o t13_xdf.o hello_image.o $(COMMON_OBJS) ../common/linker.ld
	$(CC) $(CFLAGS) t15_mux_flow.o t13_test_vectors.o t13_ctrl_c.o t13_cli_wait.o t13_cli_console.o t13_mux.o t13_cli.o t13_cli_parse.o t13_fat12.o t13_fat16_write.o t13_storage.o t13_storage_system.o t13_xdf.o hello_image.o $(COMMON_OBJS) $(LDFLAGS) -o $@

t16_human68k_volume.elf: t16_human68k_volume.o t16_human68k_volume_impl.o t16_scsi.o t16_scsi_volume.o t13_fat12.o t13_fat16_write.o t13_storage.o $(COMMON_OBJS) ../common/linker.ld
	$(CC) $(CFLAGS) t16_human68k_volume.o t16_human68k_volume_impl.o t16_scsi.o t16_scsi_volume.o t13_fat12.o t13_fat16_write.o t13_storage.o $(COMMON_OBJS) $(LDFLAGS) -o $@

# The CLI mounts through the production scsi_iocs.c path with only the
# low-level IOCS SCSI calls mocked inside the test object.
t17_human68k_cli_mount.elf: t17_human68k_cli_mount.o t13_test_vectors.o t13_ctrl_c.o t13_cli_wait.o t13_cli_console.o t13_mux.o t13_cli.o t13_cli_parse.o t13_fat12.o t13_fat16_write.o t13_storage.o t13_storage_system.o t13_xdf.o t16_human68k_volume_impl.o t16_scsi.o t16_scsi_volume.o t17_scsi_iocs.o $(COMMON_OBJS) ../common/linker.ld
	$(CC) $(CFLAGS) t17_human68k_cli_mount.o t13_test_vectors.o t13_ctrl_c.o t13_cli_wait.o t13_cli_console.o t13_mux.o t13_cli.o t13_cli_parse.o t13_fat12.o t13_fat16_write.o t13_storage.o t13_storage_system.o t13_xdf.o t16_human68k_volume_impl.o t16_scsi.o t16_scsi_volume.o t17_scsi_iocs.o $(COMMON_OBJS) $(LDFLAGS) -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Fast-switching tests link $(FAST_OBJS): their tick budgets expect a switch
# on every tick, which production cadence (10 ticks) would never deliver.
# t04: fires a single tick and requires the started worker to run in it.
t04_main_task_register.elf: t04_main_task_register.o $(FAST_OBJS) ../common/linker.ld
	$(CC) $(CFLAGS) $< $(FAST_OBJS) $(LDFLAGS) -o $@
# t07: a single tick must dispatch and terminate the started worker.
t07_task_reap.elf: t07_task_reap.o $(FAST_OBJS) ../common/linker.ld
	$(CC) $(CFLAGS) $< $(FAST_OBJS) $(LDFLAGS) -o $@

run: all
	@failed=""; \
	for t in $(TESTS); do \
		if $(MAKE) --no-print-directory run-one t=$$t; then \
			echo "PASS $$t"; \
		else \
			echo "FAIL $$t"; \
			failed="$$failed $$t"; \
		fi; \
	done; \
	if [ -n "$$failed" ]; then \
		echo "QEMU suite: FAILED:$$failed"; \
		exit 1; \
	fi; \
	echo "QEMU suite: all $(words $(TESTS)) tests passed"

run-one:
	@echo "=== $(t) ==="
	secs='$($(t)_TIMEOUT_SECS)'; \
	test -n "$$secs" || secs=$(TIMEOUT_SECS); \
	sh ../common/run_test.sh $$secs $(QEMU) $(QFLAGS) -kernel $(t).elf

clean:
	rm -rf *.o *.elf *.d build

.PHONY: FORCE
FORCE:
build/config: FORCE
	@python3 ../../../tools/build-config.py $@ $(CC) $(CFLAGS) $(ASFLAGS) $(LDFLAGS)
$(COMMON_OBJS) preempt_ctx_switch_1.o t02_register_save_entry.o t12_interrupted_start_entry.o t13_test_vectors.o t13_ctrl_c.o t13_cli_wait.o t13_cli_console.o \
	$(addsuffix .o,$(TESTS)): build/config
