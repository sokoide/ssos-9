SS_EXPORT_DIR ?= $(HOME)/tmp
ifeq ($(shell which m68k-xelf-gcc),)
$(error Cross-compilation toolchain not found. Please run: . ~/.elf2x68k)
endif

include ../config.mk

TARGET=ssos9.x
CDEFINES=-DSS_BUILD_PREEMPTIVE

include ../common-sources.mk
SRCS = main.c $(addprefix ../os/,$(SS_CORE_SRCS) $(SS_TARGET_SRCS))
ASRCS=	../os/kernel/preemptive/interrupts.s ../os/kernel/mfp_state.s

OBJDIR=build

SRC_OBJS=$(patsubst %.c,$(OBJDIR)/%.o,$(notdir $(SRCS)))
ASRC_OBJS=$(patsubst %.s,$(OBJDIR)/%.o,$(notdir $(ASRCS)))
OBJS=$(ASRC_OBJS) $(SRC_OBJS)
TARGET_FILE=$(OBJDIR)/$(TARGET)
DEBUG_ELF=$(TARGET_FILE).elf
DEBUG_DUMP=$(TARGET_FILE).dump
DEBUG_SYMBOLS=$(TARGET_FILE).symbols
DEBUG_MAP=$(TARGET_FILE).map

CC=m68k-xelf-gcc
AS=m68k-xelf-as
LD=m68k-xelf-ld
OBJDUMP=m68k-elf-objdump
READELF=m68k-elf-readelf

INCLUDE=-I$(XELF_BASE)/m68k-elf/include -I../include -I../os/kernel -I../os/kernel/preemptive
LIB=-L$(XELF_BASE)/m68k-elf/lib

CCFLAGS=$(INCLUDE) -O2 -g -MMD -MP -DLOCAL_MODE $(CDEFINES) \
	-DSS_MAX_TASKS=$(SS_MAX_TASKS) -DSS_TASK_STACK=$(SS_TASK_STACK) \
	-DSS_X_ARENA_BYTES=$(SS_X_ARENA_BYTES)
CCFLAGS += $(EXTRA_CFLAGS)
LDFLAGS=$(LIB) -lx68kiocs -nostdlib -lc -lm -lgcc

.PHONY: all clean install dump readelf debug

all: standalone

standalone: $(TARGET_FILE)
	@mkdir -p $(SS_EXPORT_DIR)
	cp $(TARGET_FILE) $(SS_EXPORT_DIR)/$(TARGET)

$(TARGET_FILE): $(OBJS)
	$(CC) $(OBJS) -o $@ -Xlinker "$(LDFLAGS)" -Wl,-Map=$(DEBUG_MAP)

dump: $(TARGET_FILE)
	$(OBJDUMP) -d -S -C $(DEBUG_ELF)

readelf: $(TARGET_FILE)
	$(READELF) -h -l -s $(DEBUG_ELF)

debug: $(TARGET_FILE)
	$(OBJDUMP) -d -S -C $(DEBUG_ELF) > $(DEBUG_DUMP)
	$(READELF) -h -l -s $(DEBUG_ELF) > $(DEBUG_SYMBOLS)
	m68k-elf-nm -n -S $(DEBUG_ELF) > $(OBJDIR)/$(TARGET).nm
	@echo "Debug files: $(DEBUG_DUMP), $(DEBUG_SYMBOLS), $(OBJDIR)/$(TARGET).nm, $(DEBUG_MAP)"

$(OBJDIR):
	@mkdir -p $(OBJDIR)

# VPATH lets the generic %.o: %.c / %.s rules find sources across the
# variant kernel dir and the shared subsystem dirs.
VPATH=../os/kernel/preemptive:../os/app:../os/kernel:../os/mem:../os/module:../os/process

$(OBJDIR)/%.o: %.c | $(OBJDIR)
	$(CC) -c $< -o $@ $(CCFLAGS)

# interrupts.s #includes scheduler_abi.h, so it must run through the C
# preprocessor; plain as would treat the #include line as a comment and emit
# unresolved SS_TCB_* symbols.
$(OBJDIR)/interrupts.o: ../os/kernel/preemptive/interrupts.s ../os/kernel/scheduler_abi.h | $(OBJDIR)
	$(CC) -x assembler-with-cpp -c $< -o $@ -I../include -I../os/kernel -I../os/kernel/preemptive \
		-Wa,--traditional-format,--register-prefix-optional,-I../include,-I../os/kernel

$(OBJDIR)/%.o: %.s | $(OBJDIR)
	$(AS) --strip-local-absolute $< -o $@ -m68000 --register-prefix-optional --traditional-format -I../include -I../os/kernel -I../os/kernel/preemptive

-include $(OBJS:.o=.d)

clean:
	rm -rf build
	rm -rf $(TARGET) *.elf *.o *.lst

.PHONY: FORCE
FORCE:
$(OBJDIR)/.config: FORCE
	@python3 ../../tools/build-config.py $@ $(CC) $(CCFLAGS) $(LDFLAGS)
$(OBJS): $(OBJDIR)/.config Makefile ../common-sources.mk $(wildcard ../include/*.mac)
$(TARGET_FILE): $(OBJDIR)/.config Makefile
