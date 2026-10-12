# Paths relative to ssos/os. Host-only sources remain in each consumer.
SS_CORE_SRCS := kernel/scheduler.c kernel/syscall.c kernel/preemptive/wakeups.c \
	mem/arena.c mem/system_arena.c
SS_CORE_SRCS += module/image.c module/module.c module/builtin.c process/process.c
SS_TARGET_SRCS := kernel/main_task.c app/cli.c
