SS_EXPORT_DIR ?= $(abspath build/export)
ifeq ($(shell which m68k-xelf-gcc),)
$(error Cross-compilation toolchain not found. Please run: . ~/.elf2x68k)
endif

DISK := ssos9.xdf
PROGRAM := ssos9.x

TOPTARGETS := dump readelf debug
SUBDIRS := boot os
MAKEDISK := ../tools/makedisk/makedisk
BOOT_BIN := boot/build/BOOT.X.bin
OS_BIN := os/build/SSOS.X.bin
OS_INPUTS := $(wildcard os/app/cli.c os/kernel/*.c os/kernel/*.h \
	os/kernel/preemptive/*.c os/kernel/preemptive/*.s os/kernel/linker.ld os/Makefile)
DISK_DIR := build/disk
DISK_FILE := $(DISK_DIR)/$(DISK)

.PHONY: $(TOPTARGETS) $(SUBDIRS) clean all standalone install

all: standalone $(DISK_FILE)

install: all
	@mkdir -p $(HOME)/tmp
	cp $(SS_EXPORT_DIR)/$(PROGRAM) $(SS_EXPORT_DIR)/$(DISK) $(HOME)/tmp/

standalone:
	$(MAKE) -C standalone standalone SS_EXPORT_DIR=$(SS_EXPORT_DIR)

dump readelf: $(SUBDIRS) standalone

debug: all
	$(MAKE) -C standalone debug

# Build the boot loader, CLI kernel, and standalone CLI binary.
$(SUBDIRS):
	$(MAKE) -C $@ $(MAKECMDGOALS)

$(DISK_FILE): $(BOOT_BIN) $(OS_BIN) $(MAKEDISK)
	@mkdir -p $(DISK_DIR)
	$(MAKEDISK) $(BOOT_BIN) $(OS_BIN) $@
	@mkdir -p $(SS_EXPORT_DIR)
	cp $@ $(SS_EXPORT_DIR)/$(DISK)

$(BOOT_BIN): FORCE boot/main.s boot/linker.ld boot/Makefile
	$(MAKE) -C boot all

$(OS_BIN): FORCE $(OS_INPUTS) common-sources.mk $(wildcard include/*.mac)
	$(MAKE) -C os all

$(MAKEDISK): ../tools/makedisk/main.c ../tools/makedisk/Makefile
	$(MAKE) -C ../tools/makedisk all

clean:
	for dir in $(SUBDIRS) standalone; do $(MAKE) -C $$dir clean; done
	rm -rf build
	rm -f $(DISK)

.PHONY: FORCE
FORCE:
