.section .text
.align   2
.globl   ss_test_install_syscall_vector
.extern  ss_syscall_entry

ss_test_install_syscall_vector:
	lea    ss_syscall_entry, a0
	move.l a0, 0x80
	rts
