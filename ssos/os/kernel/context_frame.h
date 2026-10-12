#ifndef SS_CONTEXT_FRAME_H
#define SS_CONTEXT_FRAME_H
#define SS_FRAME_REG_WORDS 15
#define SS_FRAME_ARG_BYTES 4
#define SS_FRAME_PRE_PC 60
#define SS_FRAME_PRE_SR 64
#define SS_FRAME_INTERRUPT_SR 60
#define SS_FRAME_INTERRUPT_PC 62
#define SS_FRAME_BYTES 66
#ifndef __ASSEMBLER__
#include <stddef.h>
#include <stdint.h>
typedef struct __attribute__((packed, aligned(2))) {
    uint32_t regs[SS_FRAME_REG_WORDS];
    uint32_t pc;
    uint16_t sr;
} SSPreYieldFrame;
/* MC68000 RTE consumes SR then PC after movem restores the saved registers. */
typedef struct __attribute__((packed, aligned(2))) {
    uint32_t regs[SS_FRAME_REG_WORDS];
    uint16_t sr;
    uint32_t pc;
} SSInterruptedFrame;
_Static_assert(offsetof(SSPreYieldFrame, sr) == SS_FRAME_PRE_SR, "pre SR frame");
_Static_assert(offsetof(SSPreYieldFrame, pc) == SS_FRAME_PRE_PC, "pre PC frame");
_Static_assert(sizeof(SSPreYieldFrame) == SS_FRAME_BYTES, "pre frame size");
_Static_assert(offsetof(SSInterruptedFrame, sr) == SS_FRAME_INTERRUPT_SR,
               "interrupted SR frame");
_Static_assert(offsetof(SSInterruptedFrame, pc) == SS_FRAME_INTERRUPT_PC,
               "interrupted PC frame");
_Static_assert(sizeof(SSInterruptedFrame) == SS_FRAME_BYTES,
               "interrupted frame size");
#endif
#endif
