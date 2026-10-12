.section .text
.align   2
.globl   ss_ctrl_c_trap_handler
.extern  ss_ctrl_c_pending

	| The keyboard uses TRAP #13 for Ctrl-C and BREAK. Defer policy decisions to
	| the shell task; this handler only records the event and preserves all regs.

ss_ctrl_c_trap_handler:
	move.b #1, ss_ctrl_c_pending
	rte
