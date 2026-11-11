# Preemptive-only verification entry points.
.PHONY: test test-native test-fat12 test-mux test-editor test-asm test-qemu clean

test: test-native test-fat12 test-mux test-editor

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
	$(CC) -std=c11 -Wall -Wextra -Werror unit/test_fat12_standalone.c ../ssos/os/fs/fat12.c ../ssos/os/fs/fat16_write.c ../ssos/os/fs/storage.c ../ssos/os/fs/scsi.c ../ssos/os/fs/scsi_volume.c ../ssos/os/fs/sector_adapter.c ../ssos/os/fs/xdf.c -o build/test-fat12
	$(CC) -std=c11 -Wall -Wextra -Werror unit/fat16_image_tool.c ../ssos/os/fs/fat12.c ../ssos/os/fs/fat16_write.c ../ssos/os/fs/storage.c -o build/fat16-image-tool
	$(CC) -std=c11 -Wall -Wextra -Werror unit/test_storage_integrity.c ../ssos/os/fs/fat12.c ../ssos/os/fs/fat16_write.c ../ssos/os/fs/storage.c -o build/test-storage-integrity
	./build/test-fat12
	./build/test-storage-integrity
	python3 test_disk_image.py

test-asm:
	$(MAKE) -C asm runall

test-qemu:
	$(MAKE) -C ../examples/hello all
	python3 test_mkmodule.py
	$(MAKE) test-fat12
	./build/test-fat12 ../examples/hello/build/hello-data.xdf ../examples/hello/build/hello.s9m
	$(MAKE) -C qemu run

clean:
	$(MAKE) -f Makefile.native clean
	$(MAKE) -C asm clean
	$(MAKE) -C qemu clean
