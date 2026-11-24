CC = m68k-xelf-gcc
LD = m68k-xelf-ld
CFLAGS = -m68000 -msoft-float -O2 -ffreestanding -fno-builtin -fno-common \
    -fno-stack-protector -fpic -mpcrel -ffixed-a3 -fno-jump-tables \
    -fstack-usage -I../../ssos/include

.PHONY: all clean
all: build/edit.s9m build/edit.xdf

build:
	mkdir -p $@

build/start.o: ../../examples/hello/start.s | build
	$(CC) -m68000 -c $< -o $@

build/%.o: %.c buffer.h ../../ssos/include/ssos9/user.h ../../ssos/include/ssos9/syscall.h Makefile | build
	$(CC) $(CFLAGS) -c $< -o $@

build/edit.elf: build/start.o build/main.o build/buffer.o ../../examples/hello/module.ld
	$(LD) --emit-relocs --no-relax -T ../../examples/hello/module.ld build/start.o build/main.o build/buffer.o -o $@

build/edit.s9m: build/edit.elf ../../tools/mkmodule.py
	python3 ../../tools/mkmodule.py $< --name edit --stack-size 32768 \
		--stack-usage build/main.su build/buffer.su -o $@

build/edit.xdf: build/edit.s9m ../../tools/mkfat12xdf.py
	python3 ../../tools/mkfat12xdf.py $< $@ --name EDIT.S9M

clean:
	rm -rf build
