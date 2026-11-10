# Paths relative to ssos/os. Host-only sources remain in each consumer.
SS_CORE_SRCS := kernel/scheduler.c kernel/syscall.c kernel/preemptive/wakeups.c \
	mem/arena.c mem/system_arena.c
SS_CORE_SRCS += module/image.c module/module.c module/builtin.c process/process.c
SS_CORE_SRCS += fs/fat12.c fs/fat16_write.c fs/storage.c fs/storage_system.c fs/scsi.c fs/scsi_volume.c fs/scsi_iocs.c fs/xdf.c
SS_TARGET_SRCS := kernel/main_task.c app/cli.c app/cli_wait.c app/cli_parse.c app/cli_console.c app/mux.c
