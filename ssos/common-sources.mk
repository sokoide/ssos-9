# Paths relative to ssos/os. Host-only sources remain in each consumer.
SS_CORE_SRCS := kernel/scheduler.c kernel/sched_profile.c kernel/$(SCHED)/wakeups.c
SS_TARGET_SRCS := kernel/main_task.c app/cli.c
SS_XDF_SRCS :=
