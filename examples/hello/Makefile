CC = m68k-xelf-gcc
LD = m68k-xelf-ld
CFLAGS = -m68000 -msoft-float -O2 -ffreestanding -fno-builtin \
    -fno-common -fno-stack-protector -fPIC -msep-data -ffixed-a3 \
    -fno-jump-tables -I../../ssos/include

.PHONY: all clean
all: build/hello.s9m build/hello.inc build/hello-data.xdf

build:
	mkdir -p $@

build/hello.o: hello.c ../../ssos/include/ssos9/user.h ../../ssos/include/ssos9/syscall.h | build
	$(CC) $(CFLAGS) -c $< -o $@

build/start.o: start.s | build
	$(CC) -m68000 -c $< -o $@

build/hello.elf: build/start.o build/hello.o module.ld
	$(LD) --emit-relocs --no-relax -T module.ld build/start.o build/hello.o -o $@

build/hello.s9m: build/hello.elf ../../tools/mkmodule.py
	python3 ../../tools/mkmodule.py $< --name hello -o $@

build/hello.inc: build/hello.s9m
	python3 -c 'from pathlib import Path; b=Path("$<").read_bytes(); Path("$@").write_text(",".join(str(x) for x in b) + "\n")'

build/hello-data.xdf: build/hello.s9m ../../tools/mkfat12xdf.py
	python3 ../../tools/mkfat12xdf.py $< $@

clean:
	rm -rf build
