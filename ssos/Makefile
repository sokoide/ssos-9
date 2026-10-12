SS_EXPORT_DIR ?= $(abspath build/export)
ifeq ($(shell which m68k-xelf-gcc),)
$(error Cross-compilation toolchain not found. Please run: . ~/.elf2x68k)
endif

# This repository supports the preemptive scheduler only.
override SCHED := preemptive
DISK := ssos9.xdf
PROGRAM := ssos9.x

TOPTARGETS := dump readelf
SUBDIRS := boot os
MAKEDISK := ../tools/makedisk/makedisk
BOOT_BIN := boot/build/BOOT.X.bin
OS_BIN := os/build/$(SCHED)/SSOS.X.bin
OS_INPUTS := $(wildcard os/*/*.c os/*/*.s os/*/*.h os/*/*/*.c \
	os/*/*/*.s os/*/*/*.h os/kernel/linker.ld os/Makefile)
DISK_DIR := build/disk/$(SCHED)
DISK_FILE := $(DISK_DIR)/$(DISK)

.PHONY: $(TOPTARGETS) $(SUBDIRS) clean all standalone install

all: standalone $(DISK_FILE)

install: all
	@mkdir -p $(HOME)/tmp
	cp $(SS_EXPORT_DIR)/$(PROGRAM) $(SS_EXPORT_DIR)/$(DISK) $(HOME)/tmp/

standalone:
	$(MAKE) -C standalone standalone SCHED=$(SCHED) SS_EXPORT_DIR=$(SS_EXPORT_DIR)

dump readelf: $(SUBDIRS) standalone

# boot is independent of SCHED (unified boot loader); os needs SCHED.
$(SUBDIRS):
	$(MAKE) -C $@ $(MAKECMDGOALS) SCHED=$(SCHED)

$(DISK_FILE): $(BOOT_BIN) $(OS_BIN) $(MAKEDISK)
	@mkdir -p $(DISK_DIR)
	$(MAKEDISK) $(BOOT_BIN) $(OS_BIN) $@
	@mkdir -p $(SS_EXPORT_DIR)
	cp $@ $(SS_EXPORT_DIR)/$(DISK)

$(BOOT_BIN): FORCE boot/main.s boot/linker.ld boot/Makefile
	$(MAKE) -C boot all SCHED=$(SCHED)

$(OS_BIN): FORCE $(OS_INPUTS) common-sources.mk $(wildcard include/*.mac)
	$(MAKE) -C os all SCHED=$(SCHED)

$(MAKEDISK): ../tools/makedisk/main.c ../tools/makedisk/Makefile
	$(MAKE) -C ../tools/makedisk all

clean:
	for dir in $(SUBDIRS) standalone; do $(MAKE) -C $$dir clean SCHED=$(SCHED); done
	rm -rf build
	rm -f $(DISK)

.PHONY: FORCE
FORCE:
