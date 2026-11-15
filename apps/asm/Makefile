CC = m68k-xelf-gcc
LD = m68k-xelf-ld
CFLAGS = -m68000 -msoft-float -Os -ffreestanding -fno-builtin -fno-common \
    -fno-stack-protector -fpic -mpcrel -ffixed-a3 -fno-jump-tables \
    -I../../ssos/include

CORE = lex.c parse.c encode.c obj.c ctx.c assemble.c

.PHONY: all clean
all: build/asm.s9m build/asm.xdf

build:
	mkdir -p $@

build/start.o: ../../examples/hello/start.s | build
	$(CC) -m68000 -c $< -o $@

build/%.o: %.c asm.h ../../ssos/include/ssos9/user.h ../../ssos/include/ssos9/syscall.h | build
	$(CC) $(CFLAGS) -c $< -o $@

# Shared with apps/link; an explicit rule because ../support.c cannot be a
# pattern-rule stem.
build/support.o: ../support.c | build
	$(CC) $(CFLAGS) -c $< -o $@

CORE_OBJS = $(patsubst %.c,build/%.o,$(CORE))

build/asm.elf: build/start.o $(CORE_OBJS) build/main.o build/support.o ../../examples/hello/module.ld
	$(LD) --emit-relocs --no-relax -T ../../examples/hello/module.ld build/start.o $(CORE_OBJS) build/main.o build/support.o -o $@

build/asm.s9m: build/asm.elf ../../tools/mkmodule.py
	python3 ../../tools/mkmodule.py $< --name asm --stack-size 65532 -o $@

build/asm.xdf: build/asm.s9m ../../tools/mkfat12xdf.py
	python3 ../../tools/mkfat12xdf.py $< $@ --name ASM.S9M

clean:
	rm -rf build
