    .section .text.start,"ax"
    .globl _start
_start:
    move.l %a0,-(%sp)
    move.l %d0,-(%sp)
    bsr.w main
    addq.l #8,%sp
    move.l %d0,%d1
    moveq #4,%d0
    trap #0
1:  bra.s 1b
