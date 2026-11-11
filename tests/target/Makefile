CC = m68k-xelf-gcc
LD = m68k-xelf-ld
CFLAGS = -m68000 -msoft-float -O2 -ffreestanding -fno-builtin -fno-common \
    -fno-stack-protector -fPIC -msep-data -ffixed-a3 -fno-jump-tables \
    -I../../ssos/include

.PHONY: all clean
all: build/console.s9m build/console.xdf build/BG.S9M build/background.xdf

build:
	mkdir -p $@

build/start.o: ../../examples/hello/start.s | build
	$(CC) -m68000 -c $< -o $@

build/console.o: console_smoke.c ../../ssos/include/ssos9/user.h ../../ssos/include/ssos9/syscall.h | build
	$(CC) $(CFLAGS) -c $< -o $@

build/console.elf: build/start.o build/console.o ../../examples/hello/module.ld
	$(LD) --emit-relocs --no-relax -T ../../examples/hello/module.ld build/start.o build/console.o -o $@

build/console.s9m: build/console.elf ../../tools/mkmodule.py
	python3 ../../tools/mkmodule.py $< --name console -o $@

build/console.xdf: build/console.s9m ../../tools/mkfat12xdf.py
	python3 ../../tools/mkfat12xdf.py $< $@ --name CONSOLE.S9M

build/background.o: background_smoke.c ../../ssos/include/ssos9/user.h ../../ssos/include/ssos9/syscall.h | build
	$(CC) $(CFLAGS) -c $< -o $@

build/background.elf: build/start.o build/background.o ../../examples/hello/module.ld
	$(LD) --emit-relocs --no-relax -T ../../examples/hello/module.ld build/start.o build/background.o -o $@

build/BG.S9M: build/background.elf ../../tools/mkmodule.py
	python3 ../../tools/mkmodule.py $< --name background -o $@

build/background.xdf: build/BG.S9M ../../tools/mkfat12xdf.py
	python3 ../../tools/mkfat12xdf.py $< $@ --name BG.S9M

clean:
	rm -rf build
