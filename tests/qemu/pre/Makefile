# QEMU exercises the preemptive scheduler and its context-switch path.
CC = m68k-elf-gcc
ASFLAGS = -m68000 -Wa,--register-prefix-optional -I../../../ssos/os/kernel
CFLAGS = -m68000 -O2 -g -ffreestanding -nostdlib -fno-builtin -MMD -MP \
	-Wall -Wextra -Wno-unused-parameter -I../common \
	-I../common/include -I../../../ssos/os/kernel
LDFLAGS = -T ../common/linker.ld -nostdlib -Wl,--gc-sections -lgcc
QEMU = qemu-system-m68k
QFLAGS = -M virt -cpu m68000 -nographic
TIMEOUT_SECS ?= 2

SSOS = ../../../ssos/os
TESTS = t01_round_robin t02_register_save t03_sleep_wakeup \
	t04_main_task_register t05_timerd_cadence
COMMON_OBJS = stub.o sched.o wakeups.o main_task.o preempt_ctx_switch.o

stub.o: ../common/stub.c $(SSOS)/kernel/scheduler.h
	$(CC) $(CFLAGS) -c $< -o $@
sched.o: $(SSOS)/kernel/scheduler.c
	$(CC) $(CFLAGS) -c $< -o $@
wakeups.o: $(SSOS)/kernel/preemptive/wakeups.c
	$(CC) $(CFLAGS) -c $< -o $@
main_task.o: $(SSOS)/kernel/main_task.c
	$(CC) $(CFLAGS) -c $< -o $@
preempt_ctx_switch.o: preempt_ctx_switch.s $(SSOS)/kernel/scheduler_abi.h $(SSOS)/kernel/context_frame.h
	$(CC) $(ASFLAGS) -x assembler-with-cpp -c $< -o $@
preempt_ctx_switch_10.o: preempt_ctx_switch.s $(SSOS)/kernel/scheduler_abi.h $(SSOS)/kernel/context_frame.h
	$(CC) $(ASFLAGS) -Wa,--defsym,SS_TIMERD_SWITCH_TICKS=10 -x assembler-with-cpp -c $< -o $@

-include *.d

.PHONY: all run clean
all: $(addsuffix .elf,$(TESTS))

%.elf: %.o $(COMMON_OBJS) ../common/linker.ld
	$(CC) $(CFLAGS) $< $(COMMON_OBJS) $(LDFLAGS) -o $@

t05_timerd_cadence.elf: t05_timerd_cadence.o stub.o sched.o wakeups.o main_task.o preempt_ctx_switch_10.o ../common/linker.ld
	$(CC) $(CFLAGS) $< stub.o sched.o wakeups.o main_task.o preempt_ctx_switch_10.o $(LDFLAGS) -o $@

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
$(COMMON_OBJS) preempt_ctx_switch_10.o $(addsuffix .o,$(TESTS)): build/config
