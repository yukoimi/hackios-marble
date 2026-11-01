/* Board candidate only; never passed to a hardware function by this header.
 * Layout: exact Mu I2C GetConfig at 3150..37e4. Geometry/pin function from
 * marble DT and pinned waipio pinctrl/GCC sources. No controller acceptance. */
#ifndef MARBLE_I2C_BOARD_H
#define MARBLE_I2C_BOARD_H
#include <stdint.h>
#include <stddef.h>
typedef struct {uint32_t se_khz,bus_khz;uint8_t divider,cycle,high,low;} MarbleI2cTiming;
typedef struct {
    uint64_t core_base,common_base;
    uint32_t offset;
    uint8_t qup,core_index,se_index,gpi_index;
    uint32_t irq,polled,min_dma,scl,sda,tcsr_offset;
    uint64_t tcsr_base;
    uint32_t tcsr_value,reserved;
    const char *const *common_clocks;
    const char *const *se_clocks;
    const MarbleI2cTiming *timings;
} MarbleI2cBoardConfig;
_Static_assert(sizeof(MarbleI2cTiming)==12,"native I2C timing ABI");
_Static_assert(sizeof(MarbleI2cBoardConfig)==88,"native I2C configuration ABI");
_Static_assert(offsetof(MarbleI2cBoardConfig,qup)==0x14 && offsetof(MarbleI2cBoardConfig,polled)==0x1c &&
    offsetof(MarbleI2cBoardConfig,scl)==0x24 && offsetof(MarbleI2cBoardConfig,tcsr_base)==0x30 &&
    offsetof(MarbleI2cBoardConfig,common_clocks)==0x40 && offsetof(MarbleI2cBoardConfig,timings)==0x50,"native I2C field offsets");
static const char *const marble_i2c_board_common[]={"gcc_qupv3_wrap2_core_clk","gcc_qupv3_wrap2_core_2x_clk",
    "gcc_qupv3_wrap_2_m_ahb_clk","gcc_qupv3_wrap_2_s_ahb_clk",0};
static const char *const marble_i2c_board_se[]={"gcc_qupv3_wrap2_s0_clk",0};
/* Copy only the exact 100 kHz/19.2 MHz entry and terminator from ROM DT.
 * This is configuration data, not a frequency request. */
static const MarbleI2cTiming marble_i2c_board_timings[]={{19200,100,7,26,10,11},{0}};
static MarbleI2cBoardConfig marble_i2c_board_candidate(void)
{
    MarbleI2cBoardConfig c={0};c.core_base=0x800000;c.common_base=0x8c0000;c.offset=0x80000;
    c.qup=2;c.core_index=16;c.se_index=0;c.gpi_index=0;c.polled=1;
    c.scl=0x18391;c.sda=0x18381;c.tcsr_base=0x1fc0000;
    c.common_clocks=marble_i2c_board_common;c.se_clocks=marble_i2c_board_se;c.timings=marble_i2c_board_timings;
    return c;
}
static int marble_i2c_board_candidate_valid(const MarbleI2cBoardConfig *c)
{
    return c && c->core_base==0x800000 && c->common_base==0x8c0000 && c->offset==0x80000 &&
        c->qup==2 && c->core_index==16 && !c->se_index && !c->gpi_index && !c->irq && c->polled==1 && !c->min_dma &&
        c->scl==0x18391 && c->sda==0x18381 && c->tcsr_base==0x1fc0000 && !c->tcsr_offset && !c->tcsr_value && !c->reserved &&
        c->common_clocks==marble_i2c_board_common && c->se_clocks==marble_i2c_board_se && c->timings==marble_i2c_board_timings;
}
#endif
