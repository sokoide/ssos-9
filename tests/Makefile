# Preemptive-only verification entry points.
.PHONY: test test-context test-native test-fat12 test-mux test-editor test-asm-host test-asm-golden test-link-host test-link-golden test-link test-qemu clean

test: test-verdict test-context test-native test-fat12 test-mux test-editor test-asm-host test-link-host test-link-wrap

# Production ISR <-> QEMU port sync check (tools/check-context.py).
test-context:
	python3 ../tools/check-context.py

test-editor:
	@mkdir -p build
	$(CC) -std=c11 -Wall -Wextra -Werror unit/test_editor_buffer.c ../apps/edit/buffer.c -o build/test-editor
	./build/test-editor
	$(CC) -std=c11 -Wall -Wextra -Werror -DEDIT_HOST_TEST -Dmain=editor_main -Iunit/editor_fake -c ../apps/edit/main.c -o build/editor-main.o
	$(CC) -std=c11 -Wall -Wextra -Werror unit/test_editor_app.c ../apps/edit/buffer.c build/editor-main.o -o build/test-editor-app
	./build/test-editor-app

test-mux:
	@mkdir -p build
	$(CC) -std=c11 -Wall -Wextra -Werror unit/test_mux.c ../ssos/os/app/mux.c -o build/test-mux
	./build/test-mux

test-native:
	$(MAKE) -f Makefile.native test

test-fat12:
	@mkdir -p build
	$(CC) -std=c11 -Wall -Wextra -Werror unit/test_human68k_volume.c ../ssos/os/fs/human68k_volume.c -o build/test-human68k-volume
	./build/test-human68k-volume
	$(CC) -std=c11 -Wall -Wextra -Werror unit/test_fat12_standalone.c ../ssos/os/fs/fat12.c ../ssos/os/fs/fat16_write.c ../ssos/os/fs/storage.c ../ssos/os/fs/scsi.c ../ssos/os/fs/scsi_volume.c ../ssos/os/fs/human68k_volume.c ../ssos/os/fs/sector_adapter.c ../ssos/os/fs/xdf.c -o build/test-fat12
	$(CC) -std=c11 -Wall -Wextra -Werror unit/test_human68k_storage.c ../ssos/os/fs/fat12.c ../ssos/os/fs/fat16_write.c ../ssos/os/fs/storage.c ../ssos/os/fs/scsi.c ../ssos/os/fs/scsi_volume.c ../ssos/os/fs/human68k_volume.c -o build/test-human68k-storage
	./build/test-human68k-storage
	$(CC) -std=c11 -Wall -Wextra -Werror unit/fat16_image_tool.c ../ssos/os/fs/fat12.c ../ssos/os/fs/fat16_write.c ../ssos/os/fs/storage.c -o build/fat16-image-tool
	$(CC) -std=c11 -Wall -Wextra -Werror unit/test_storage_integrity.c ../ssos/os/fs/fat12.c ../ssos/os/fs/fat16_write.c ../ssos/os/fs/storage.c -o build/test-storage-integrity
	./build/test-fat12
	./build/test-storage-integrity
	python3 test_disk_image.py

ASM_CORE = ../apps/asm/lex.c ../apps/asm/parse.c ../apps/asm/encode.c ../apps/asm/obj.c ../apps/asm/ctx.c ../apps/asm/assemble.c
ASM_HOST_FLAGS = -std=c11 -Wall -Wextra -Werror -I../apps/asm

test-asm-host:
	@mkdir -p build
	$(CC) $(ASM_HOST_FLAGS) unit/test_asm_parse.c ../apps/asm/lex.c ../apps/asm/parse.c ../apps/asm/ctx.c -o build/test-asm-parse
	./build/test-asm-parse
	$(CC) $(ASM_HOST_FLAGS) unit/test_asm_encode.c ../apps/asm/encode.c ../apps/asm/ctx.c -o build/test-asm-encode
	./build/test-asm-encode
	$(CC) $(ASM_HOST_FLAGS) unit/test_asm_obj.c ../apps/asm/obj.c ../apps/asm/ctx.c -o build/test-asm-obj
	./build/test-asm-obj
	$(CC) $(ASM_HOST_FLAGS) unit/asm_host_main.c $(ASM_CORE) -o build/asm-host
	@# Negative test for the default-output 8.3 name rule: assemble a REAL
	@# file whose name is far too long, and require the rejection to be the
	@# name rule ("source name is not 8.3"), not a file-open failure.
	@mkdir -p build; \
	long_src=$$(mktemp build/$$(python3 -c "print('a'*64)")XXXXXXXX.S); \
	printf '.dc.w 0x4e75\n.end\n' >"$$long_src"; \
	if ./build/asm-host "$$long_src" 2>build/longname.err; then \
	    rm -f "$$long_src" build/longname.err; \
	    echo "long source name accepted as default output"; exit 1; fi; \
	if grep -q "source name is not 8.3" build/longname.err; then \
	    rm -f "$$long_src" build/longname.err; \
	else \
	    cat build/longname.err >&2; \
	    rm -f "$$long_src" build/longname.err; \
	    echo "long source name rejected for the wrong reason"; exit 1; fi

test-asm-golden: test-asm-host
	python3 asm_golden.py

test-asm:
	$(MAKE) -C asm runall
	$(MAKE) test-asm-golden

LINK_CORE = ../apps/link/readobj.c ../apps/link/layout.c ../apps/link/fold.c ../apps/link/image.c ../apps/support.c
LINK_HOST_FLAGS = -std=c11 -Wall -Wextra -Werror -I../apps/link

test-link-host:
	@mkdir -p build
	$(CC) $(LINK_HOST_FLAGS) unit/link_host_main.c $(LINK_CORE) -o build/link-host

# Crafted-header wrap regressions (F-01/R-10) + CRC reference match (R-13).
test-link-wrap:
	@mkdir -p build
	$(CC) $(LINK_HOST_FLAGS) unit/test_link_wrap.c $(LINK_CORE) -o build/test-link-wrap
	./build/test-link-wrap

test-link-golden: test-link-host
	python3 link_golden.py

test-link: test-link-golden
	$(MAKE) test-asm-golden

test-qemu:
	$(MAKE) -C ../examples/hello all
	$(MAKE) -C ../apps/asm all
	$(MAKE) test-asm-host
	python3 test_mkmodule.py
	$(MAKE) test-fat12
	./build/test-fat12 ../examples/hello/build/hello-data.xdf ../examples/hello/build/hello.s9m
	$(MAKE) -C qemu run

clean:
	$(MAKE) -f Makefile.native clean
	$(MAKE) -C asm clean
	$(MAKE) -C qemu clean
	rm -f build/test-asm-parse build/test-asm-encode build/test-asm-obj build/asm-host build/link-host build/test-link-wrap

.PHONY: test-verdict
test-verdict:
	sh test_qemu_verdict.sh
