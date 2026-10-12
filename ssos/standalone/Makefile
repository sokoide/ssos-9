SS_EXPORT_DIR ?= $(HOME)/tmp
ifeq ($(shell which m68k-xelf-gcc),)
$(error Cross-compilation toolchain not found. Please run: . ~/.elf2x68k)
endif

# Human68K launcher for the preemptive OS.
override SCHED := preemptive

TARGET=ssos9.x
CDEFINES=-DSS_BUILD_PREEMPTIVE

KDIR=../os/kernel/$(SCHED)

include ../common-sources.mk
SRCS = main.c $(addprefix ../os/,$(SS_CORE_SRCS) $(SS_TARGET_SRCS))
ASRCS=	$(KDIR)/interrupts.s ../os/kernel/mfp_state.s

OBJDIR=build/$(SCHED)

SRC_OBJS=$(patsubst %.c,$(OBJDIR)/%.o,$(notdir $(SRCS)))
ASRC_OBJS=$(patsubst %.s,$(OBJDIR)/%.o,$(notdir $(ASRCS)))
OBJS=$(ASRC_OBJS) $(SRC_OBJS)
TARGET_FILE=$(OBJDIR)/$(TARGET)

CC=m68k-xelf-gcc
AS=m68k-xelf-as
LD=m68k-xelf-ld
OBJDUMP=m68k-elf-objdump
READELF=m68k-elf-readelf

INCLUDE=-I$(XELF_BASE)/m68k-elf/include -I../include -I../os/kernel -I$(KDIR)
LIB=-L$(XELF_BASE)/m68k-elf/lib

CCFLAGS=$(INCLUDE) -O2 -g -MMD -MP -DLOCAL_MODE $(CDEFINES)
CCFLAGS += $(EXTRA_CFLAGS)
LDFLAGS=$(LIB) -lx68kiocs -nostdlib -lc -lm -lgcc

.PHONY: all clean install dump readelf

all: standalone

standalone: $(TARGET_FILE)
	@mkdir -p $(SS_EXPORT_DIR)
	cp $(TARGET_FILE) $(SS_EXPORT_DIR)/$(TARGET)

$(TARGET_FILE): $(OBJS)
	$(CC) $(OBJS) -o $@ -Xlinker "$(LDFLAGS)"

dump: $(TARGET_FILE)
	$(OBJDUMP) -d -S -C $(TARGET_FILE)

readelf: $(TARGET_FILE)
	$(READELF) -a $(TARGET_FILE)

$(OBJDIR):
	@mkdir -p $(OBJDIR)

# VPATH lets the generic %.o: %.c / %.s rules find sources across the
# variant kernel dir and the shared subsystem dirs.
VPATH=$(KDIR):../os/app:../os/kernel

$(OBJDIR)/%.o: %.c | $(OBJDIR)
	$(CC) -c $< -o $@ $(CCFLAGS)

$(OBJDIR)/trap14.o: trap14.s | $(OBJDIR)
	$(AS) --strip-local-absolute $< -o $@ -m68000 --register-prefix-optional --traditional-format -I../include

# interrupts.s #includes scheduler_abi.h, so it must run through the C
# preprocessor; plain as would treat the #include line as a comment and emit
# unresolved SS_TCB_* symbols.
$(OBJDIR)/interrupts.o: $(KDIR)/interrupts.s ../os/kernel/scheduler_abi.h | $(OBJDIR)
	$(CC) -x assembler-with-cpp -c $< -o $@ -I../include -I../os/kernel -I$(KDIR) \
		-Wa,--traditional-format,--register-prefix-optional,-I../include,-I../os/kernel

$(OBJDIR)/%.o: %.s | $(OBJDIR)
	$(AS) --strip-local-absolute $< -o $@ -m68000 --register-prefix-optional --traditional-format -I../include -I../os/kernel -I$(KDIR)

-include $(OBJS:.o=.d)

clean:
	rm -rf build
	rm -rf $(TARGET) obj-preemptive *.elf *.o *.lst

.PHONY: FORCE
FORCE:
$(OBJDIR)/.config: FORCE
	@python3 ../../tools/build-config.py $@ $(CC) $(CCFLAGS) $(LDFLAGS)
$(OBJS): $(OBJDIR)/.config Makefile ../common-sources.mk $(wildcard ../include/*.mac)
$(TARGET_FILE): $(OBJDIR)/.config Makefile
