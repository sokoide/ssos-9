# Keep the C task table and linker-reserved stack pool in sync.
SS_MAX_TASKS ?= 10
# Stack of a task created without its own stack: the idle, reaper and
# heartbeat kernel tasks. They run no console or storage code.
SS_TASK_STACK ?= 4096
# Stack of a kernel-entry child process (a mux shell or a background
# builtin), taken from the arena. Kernel paths now use a few hundred bytes
# of the caller's stack; 64 KiB leaves room for IOCS and future growth.
SS_KERNEL_STACK ?= 65536
SS_RAM_END ?= 0x00C00000
# Arena of the standalone (.x) build: kernel-entry children take
# SS_KERNEL_STACK each and module processes take their declared stack.
SS_X_ARENA_BYTES ?= 1048576
