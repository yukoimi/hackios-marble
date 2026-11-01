/* Read-only prerequisites from pinned Xiaomi gen7_load_{rsc,pdc}_ucode.
 * This describes Gen7_4_0, never the Gen7_6_0 sequence offset. */
#ifndef MARBLE_GMU_SEQUENCERS_H
#define MARBLE_GMU_SEQUENCERS_H
#include <stdint.h>
typedef struct {unsigned pdc,offset;uint32_t value,mask;} MarbleSequenceRegister;
static const MarbleSequenceRegister marble_sequence_regs[]={
    {0,0x10,0x1000000,0x1000000},
    {0,0x20,0x4520,~0u},{0,0x24,0x4510,~0u},{0,0x28,0x4514,~0u},{0,0x2c,1,~0u},
    {0,0x34,0,~0u},{0,0x38,0,~0u},{0,0x3c,0,~0u},{0,0x40,0,~0u},
    {0,0x44,0,~0u},{0,0x48,0x80000000,~0u},{0,0x400,0,~0u},{0,0x404,0,~0u},
    {0,0x600,0xeaaae5a0,~0u},{0,0x604,0xe1a1ebab,~0u},{0,0x608,0xa2e0a581,~0u},
    {0,0x60c,0xecac82e2,~0u},{0,0x610,0x0020edad,~0u},
    {1,0x4500,0x80000001,~0u},{1,0x4520,0,~0u}
};
#define MARBLE_SEQUENCE_REGS (sizeof(marble_sequence_regs)/sizeof(marble_sequence_regs[0]))
#endif
