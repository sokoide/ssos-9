# Preemptive-only verification entry points.
.PHONY: test test-native test-asm test-qemu clean

test: test-native

test-native:
	$(MAKE) -f Makefile.native test

test-asm:
	$(MAKE) -C asm runall

test-qemu:
	$(MAKE) -C qemu run

clean:
	$(MAKE) -f Makefile.native clean
	$(MAKE) -C asm clean
	$(MAKE) -C qemu clean
