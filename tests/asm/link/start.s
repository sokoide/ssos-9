	;       Linker sample: entry object (START.O). First input, so its text[0] is the
	;       S9M1 entry. Calls main in MAIN.O (PC16 across objects) and exits with the
	;       status main returns.
	.text
	.globl  _start
	.extern main

_start:
	bsr.w  main
	move.l %d0, %d1
	moveq  #4, %d0
	trap   #0
