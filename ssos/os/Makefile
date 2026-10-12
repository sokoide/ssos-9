ifeq ($(shell which m68k-xelf-gcc),)
$(error Cross-compilation toolchain not found. Please run: . ~/.elf2x68k)
endif

# The kernel uses Timer D preemption exclusively.
override SCHED := preemptive

# Per-variant sources live under kernel/$(SCHED)/; shared kernel code stays at kernel/.
KDIR=kernel/$(SCHED)

TARGET=SSOS.X
include ../common-sources.mk
SRCS = $(SS_CORE_SRCS) $(SS_TARGET_SRCS) $(SS_XDF_SRCS) kernel/premain.c
ASRCS= \
	kernel/entry.s \
	kernel/mfp_state.s \
	$(KDIR)/interrupts.s

BUILD_DIR=build/$(SCHED)
OBJS=$(addprefix $(BUILD_DIR)/,$(ASRCS:.s=.o) $(SRCS:.c=.o))
TARGET_ELF=$(BUILD_DIR)/$(TARGET).elf
TARGET_BIN=$(BUILD_DIR)/$(TARGET).bin

CC=m68k-xelf-gcc
AS=m68k-xelf-as
LD=m68k-xelf-ld
OBJCOPY=m68k-xelf-objcopy
OBJDUMP=m68k-elf-objdump
READELF=m68k-elf-readelf

INCLUDE=-I$(XELF_BASE)/m68k-elf/include -I../include -I./kernel -I./$(KDIR) -I./app
LIB=-L$(XELF_BASE)/m68k-elf/lib -L$(shell dirname $(shell m68k-xelf-gcc -print-libgcc-file-name))

CCFLAGS=$(INCLUDE) -g -O2 -MMD -MP
CCFLAGS += $(EXTRA_CFLAGS)
LDFLAGS=$(LIB) -lx68kiocs -Tkernel/linker.ld -nostdlib -lc -lm -lgcc -lnosys
ASFLAGS=-m68000 --register-prefix-optional --traditional-format -I../include -I./kernel

.PHONY: all clean dump readelf

all: $(TARGET_BIN)

$(TARGET_BIN): $(TARGET_ELF)
	$(OBJCOPY) -O binary $< $@

$(TARGET_ELF): $(OBJS) kernel/linker.ld
	@mkdir -p $(dir $@)
	$(LD) --strip-all $(OBJS) -o $@ $(LDFLAGS) -e entry

dump: $(TARGET_ELF)
	$(OBJDUMP) -d -S -C $(TARGET_ELF)

readelf: $(TARGET_ELF)
	$(READELF) -a $(TARGET_ELF)

$(BUILD_DIR)/%.o: %.s
	@mkdir -p $(dir $@)
	$(AS) --strip-local-absolute $< -o $@ $(ASFLAGS)

$(BUILD_DIR)/kernel/$(SCHED)/interrupts.o: kernel/$(SCHED)/interrupts.s kernel/scheduler_abi.h kernel/context_frame.h
	@mkdir -p $(dir $@)
	$(CC) -x assembler-with-cpp -c $< -o $@ $(CCFLAGS) -I../include -I./kernel -I./$(KDIR) \
		-Wa,--traditional-format,--register-prefix-optional,-I../include,-I./kernel

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) -c $< -o $@ $(CCFLAGS)

-include $(OBJS:.o=.d)

clean:
	rm -rf build
	rm -f $(TARGET) $(TARGET).bin $(TARGET).elf $(ASRCS:.s=.o) $(SRCS:.c=.o)
	rm -f kernel/*.o kernel/*.lst kernel/preemptive/*.o

.PHONY: FORCE
FORCE:
$(BUILD_DIR)/.config: FORCE
	@python3 ../../tools/build-config.py $@ $(CC) $(CCFLAGS) $(ASFLAGS) $(LDFLAGS)
$(OBJS): $(BUILD_DIR)/.config Makefile ../common-sources.mk $(wildcard ../include/*.mac)
$(TARGET_ELF): $(BUILD_DIR)/.config Makefile
