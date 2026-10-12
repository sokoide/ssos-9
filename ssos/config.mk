# Keep the C task table and linker-reserved stack pool in sync.
SS_MAX_TASKS ?= 10
SS_TASK_STACK ?= 4096
SS_RAM_END ?= 0x00C00000
SS_X_ARENA_BYTES ?= 262144
