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
TIMEOUT_SECS ?= 2

SSOS = ../../../ssos/os
TESTS = t01_round_robin t02_register_save t03_sleep_wakeup \
	t04_main_task_register t05_timerd_cadence t06_idle_sleep t07_task_reap \
	t08_cancel_waiting t09_arena t10_module_image t11_module_registry \
	t12_interrupted_start t13_process_spawn
COMMON_OBJS = stub.o sched.o syscall.o syscall_entry.o wakeups.o main_task.o arena.o image.o module.o builtin.o process.o \
	system_arena_local.o preempt_ctx_switch.o

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
builtin.o: $(SSOS)/module/builtin.c $(SSOS)/module/builtin.h $(SSOS)/module/module.h
	$(CC) $(CFLAGS) -I$(SSOS)/module -c $< -o $@
process.o: $(SSOS)/process/process.c $(SSOS)/process/process.h
	$(CC) $(CFLAGS) -I$(SSOS)/module -I$(SSOS)/mem -I$(SSOS)/process -c $< -o $@
system_arena_local.o: $(SSOS)/mem/system_arena.c $(SSOS)/mem/system_arena.h
	$(CC) $(CFLAGS) -DLOCAL_MODE -I$(SSOS)/mem -c $< -o $@
preempt_ctx_switch.o: preempt_ctx_switch.s $(SSOS)/kernel/scheduler_abi.h $(SSOS)/kernel/context_frame.h
	$(CC) $(ASFLAGS) -x assembler-with-cpp -c $< -o $@
preempt_ctx_switch_10.o: preempt_ctx_switch.s $(SSOS)/kernel/scheduler_abi.h $(SSOS)/kernel/context_frame.h
	$(CC) $(ASFLAGS) -Wa,--defsym,SS_TIMERD_SWITCH_TICKS=10 -x assembler-with-cpp -c $< -o $@
t12_interrupted_start_entry.o: t12_interrupted_start.s
	$(CC) $(ASFLAGS) -c $< -o $@
t13_test_vectors.o: t13_test_vectors.s
	$(CC) $(ASFLAGS) -c $< -o $@
t02_register_save_entry.o: t02_register_save.s
	$(CC) $(ASFLAGS) -c $< -o $@

-include *.d

.PHONY: all run clean
all: $(addsuffix .elf,$(TESTS))

%.elf: %.o $(COMMON_OBJS) ../common/linker.ld
	$(CC) $(CFLAGS) $< $(COMMON_OBJS) $(LDFLAGS) -o $@

t02_register_save.elf: t02_register_save.o t02_register_save_entry.o $(COMMON_OBJS) ../common/linker.ld
	$(CC) $(CFLAGS) $< t02_register_save_entry.o $(COMMON_OBJS) $(LDFLAGS) -o $@

t05_timerd_cadence.elf: t05_timerd_cadence.o stub.o sched.o wakeups.o main_task.o preempt_ctx_switch_10.o ../common/linker.ld
	$(CC) $(CFLAGS) $< stub.o sched.o wakeups.o main_task.o preempt_ctx_switch_10.o $(LDFLAGS) -o $@

t12_interrupted_start.elf: t12_interrupted_start.o t12_interrupted_start_entry.o $(COMMON_OBJS) ../common/linker.ld
	$(CC) $(CFLAGS) t12_interrupted_start.o t12_interrupted_start_entry.o $(COMMON_OBJS) $(LDFLAGS) -o $@

t13_process_spawn.elf: t13_process_spawn.o t13_test_vectors.o $(COMMON_OBJS) ../common/linker.ld
	$(CC) $(CFLAGS) t13_process_spawn.o t13_test_vectors.o $(COMMON_OBJS) $(LDFLAGS) -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

run: all
	@set -e; for t in $(TESTS); do \
		echo "=== $$t ==="; \
		sh ../common/run_test.sh $(TIMEOUT_SECS) $(QEMU) $(QFLAGS) -kernel $$t.elf; \
	done

clean:
	rm -rf *.o *.elf *.d build

.PHONY: FORCE
FORCE:
build/config: FORCE
	@python3 ../../../tools/build-config.py $@ $(CC) $(CFLAGS) $(ASFLAGS) $(LDFLAGS)
$(COMMON_OBJS) preempt_ctx_switch_10.o t02_register_save_entry.o t12_interrupted_start_entry.o t13_test_vectors.o \
	$(addsuffix .o,$(TESTS)): build/config
