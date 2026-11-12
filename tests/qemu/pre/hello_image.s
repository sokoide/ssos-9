.section .rodata
.balign  4
.globl   ss_test_hello_image
.globl   ss_test_hello_image_end

ss_test_hello_image:
	.incbin "../../../examples/hello/build/hello.s9m"

ss_test_hello_image_end:
	.balign 4
	.globl  ss_test_data_disk

ss_test_data_disk:
	.incbin "../../../examples/hello/build/hello-data.xdf"
	.balign 4
	.globl  ss_test_edit_image
	.globl  ss_test_edit_image_end

ss_test_edit_image:
	.incbin "../../../apps/edit/build/edit.s9m"

ss_test_edit_image_end:
	.balign 4
	.globl  ss_test_asm_image
	.globl  ss_test_asm_image_end

ss_test_asm_image:
	.incbin "../../../apps/asm/build/asm.s9m"

ss_test_asm_image_end:
	.balign 4
	.globl  ss_test_asm_source
	.globl  ss_test_asm_source_end

ss_test_asm_source:
	.incbin "../../../tests/asm/qemu-prog.s"

ss_test_asm_source_end:
	.balign 4
	.globl  ss_test_asm_expected
	.globl  ss_test_asm_expected_end

ss_test_asm_expected:
	.incbin "build/asm-expected.bin"

ss_test_asm_expected_end:
	.balign 4
	.globl  ss_test_ld_image
	.globl  ss_test_ld_image_end

ss_test_ld_image:
	.incbin "../../../apps/link/build/ld.s9m"

ss_test_ld_image_end:
	.balign 4
	.globl  ss_test_link_start_src
	.globl  ss_test_link_start_src_end

ss_test_link_start_src:
	.incbin "../../../tests/asm/link/start.s"

ss_test_link_start_src_end:
	.balign 4
	.globl  ss_test_link_main_src
	.globl  ss_test_link_main_src_end

ss_test_link_main_src:
	.incbin "../../../tests/asm/link/main.s"

ss_test_link_main_src_end:
	.balign 4
	.globl  ss_test_link_obj_src
	.globl  ss_test_link_obj_src_end

ss_test_link_obj_src:
	.incbin "../../../tests/asm/link/obj.s"

ss_test_link_obj_src_end:
	.balign 4
	.globl  ss_test_link_badmain_src
	.globl  ss_test_link_badmain_src_end

ss_test_link_badmain_src:
	.incbin "../../../tests/asm/link/badmain.s"

ss_test_link_badmain_src_end:
	.balign 4
	.globl  ss_test_link_expected
	.globl  ss_test_link_expected_end

ss_test_link_expected:
	.incbin "build/link-expected.bin"

ss_test_link_expected_end:
