REQUIRED_TOOLCHAIN_COMMANDS := m68k-xelf-as m68k-xelf-ld m68k-xelf-objcopy
ifneq ($(strip $(MAKECMDGOALS)),)
ifneq ($(strip $(filter-out clean,$(MAKECMDGOALS))),)
MISSING_TOOLCHAIN_COMMANDS := $(shell for command in $(REQUIRED_TOOLCHAIN_COMMANDS); do command -v $$command >/dev/null 2>&1 || printf '%s ' $$command; done)
ifneq ($(strip $(MISSING_TOOLCHAIN_COMMANDS)),)
$(error Cross-compilation toolchain commands not found: $(MISSING_TOOLCHAIN_COMMANDS). Please run: . ~/.elf2x68k)
endif
endif
else
MISSING_TOOLCHAIN_COMMANDS := $(shell for command in $(REQUIRED_TOOLCHAIN_COMMANDS); do command -v $$command >/dev/null 2>&1 || printf '%s ' $$command; done)
ifneq ($(strip $(MISSING_TOOLCHAIN_COMMANDS)),)
$(error Cross-compilation toolchain commands not found: $(MISSING_TOOLCHAIN_COMMANDS). Please run: . ~/.elf2x68k)
endif
endif

TARGET=BOOT.X
BUILD_DIR=build
TARGET_ELF=$(BUILD_DIR)/$(TARGET).elf
TARGET_BIN=$(BUILD_DIR)/$(TARGET).bin
AS=m68k-xelf-as
LD=m68k-xelf-ld
OBJCOPY=m68k-xelf-objcopy
OBJDUMP=m68k-elf-objdump
READELF=m68k-elf-readelf

ASFLAGS=-m68000 --register-prefix-optional --traditional-format -I../include

.PHONY: all clean dump readelf

all: $(TARGET_BIN)

$(TARGET_BIN): $(TARGET_ELF)
	$(OBJCOPY) -O binary $< $@

$(TARGET_ELF): $(BUILD_DIR)/main.o linker.ld
	@mkdir -p $(dir $@)
	$(LD) --strip-all $< -o $@ -Tlinker.ld -e boot

dump: $(TARGET_ELF)
	$(OBJDUMP) -d $(TARGET_ELF)

readelf: $(TARGET_ELF)
	$(READELF) -a $(TARGET_ELF)

$(BUILD_DIR)/main.o: main.s
	@mkdir -p $(dir $@)
	$(AS) --strip-local-absolute $< -o $@ $(ASFLAGS)

clean:
	rm -rf build
	rm -f $(TARGET) $(TARGET).bin $(TARGET).elf main.o

.PHONY: FORCE
FORCE:
$(BUILD_DIR)/.config: FORCE
	@python3 ../../tools/build-config.py $@ $(AS) $(ASFLAGS)
$(BUILD_DIR)/main.o: $(BUILD_DIR)/.config Makefile ../common-sources.mk $(wildcard ../include/*.mac)
