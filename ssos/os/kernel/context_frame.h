#ifndef SS_CONTEXT_FRAME_H
#define SS_CONTEXT_FRAME_H
#define SS_FRAME_REG_WORDS 15
#define SS_FRAME_ARG_BYTES 4
#define SS_FRAME_PRE_PC 60
#define SS_FRAME_PRE_SR 64
#define SS_FRAME_BYTES 66
#ifndef __ASSEMBLER__
#include <stddef.h>
#include <stdint.h>
typedef struct __attribute__((packed, aligned(2))) {
    uint32_t regs[SS_FRAME_REG_WORDS];
    uint32_t pc;
    uint16_t sr;
} SSPreYieldFrame;
_Static_assert(offsetof(SSPreYieldFrame, sr) == SS_FRAME_PRE_SR, "pre SR frame");
_Static_assert(offsetof(SSPreYieldFrame, pc) == SS_FRAME_PRE_PC, "pre PC frame");
_Static_assert(sizeof(SSPreYieldFrame) == SS_FRAME_BYTES, "pre frame size");
#endif
#endif
