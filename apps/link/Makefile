CC = m68k-xelf-gcc
LD = m68k-xelf-ld
HOSTCC = cc
CFLAGS = -m68000 -msoft-float -Os -ffreestanding -fno-builtin -fno-common \
    -fno-stack-protector -fpic -mpcrel -ffixed-a3 -fno-jump-tables \
    -fstack-usage -I../../ssos/include
HOSTCFLAGS = -O2 -Wall -I.

CORE = readobj.c layout.c fold.c image.c

.PHONY: all host clean
all: build/ld.s9m build/ld.xdf

host: build/link-host

build:
	mkdir -p $@

build/start.o: ../../examples/hello/start.s | build
	$(CC) -m68000 -c $< -o $@

build/%.o: %.c link.h ../../ssos/include/ssos9/user.h ../../ssos/include/ssos9/syscall.h Makefile | build
	$(CC) $(CFLAGS) -c $< -o $@

# Shared with apps/asm; an explicit rule because ../support.c cannot be a
# pattern-rule stem.
build/support.o: ../support.c Makefile | build
	$(CC) $(CFLAGS) -c $< -o $@

CORE_OBJS = $(patsubst %.c,build/%.o,$(CORE))
LINK_OBJS = $(CORE_OBJS) build/main.o build/support.o

build/ld.elf: build/start.o $(LINK_OBJS) ../../examples/hello/module.ld
	$(LD) --emit-relocs --no-relax -T ../../examples/hello/module.ld build/start.o $(LINK_OBJS) -o $@

build/ld.s9m: build/ld.elf ../../tools/mkmodule.py
	python3 ../../tools/mkmodule.py $< --name ld --stack-size 131072 \
		--stack-usage $(LINK_OBJS:.o=.su) -o $@

build/ld.xdf: build/ld.s9m ../../tools/mkfat12xdf.py
	python3 ../../tools/mkfat12xdf.py $< $@ --name LD.S9M

# Host driver for manual verification (stdio I/O; no support.c — the
# host toolchain provides memcpy and native division).
build/link-host: host_main.c $(CORE) link.h | build
	$(HOSTCC) $(HOSTCFLAGS) host_main.c $(CORE) -o $@

clean:
	rm -rf build
