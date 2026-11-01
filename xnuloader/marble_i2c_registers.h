/* Bounded passive inventory from pinned Xiaomi msm-geni-se.h. No FIFO data,
 * interrupt clear, command, pin, reset, or firmware writes are allowed. */
#ifndef MARBLE_I2C_REGISTERS_H
#define MARBLE_I2C_REGISTERS_H
#include <stdint.h>
static const uint64_t marble_i2c_ro_addresses[]={
    0x8c0004, /* wrapper QUPV3_HW_VER */
    0x880040, /* GENI_STATUS */
    0x880060, /* GENI_CLK_CTRL_RO */
    0x880064, /* GENI_IF_FIFO_DISABLE_RO */
    0x880068, /* GENI_FW_REVISION_RO */
    0x88006c, /* GENI_FW_S_REVISION_RO */
    0x880e24, /* SE_HW_PARAM_0 */
    0x880e28  /* SE_HW_PARAM_1 */
};
static int marble_i2c_ro_address_allowed(uint64_t address)
{
    for(unsigned i=0;i<8;i++)if(address==marble_i2c_ro_addresses[i])return 1;
    return 0;
}
static int marble_i2c_ro_identity(const uint32_t v[8])
{
    unsigned tx_depth=(v[6]>>16)&63,rx_depth=(v[7]>>16)&63;
    unsigned tx_width=(v[6]>>24)&63,rx_width=(v[7]>>24)&63;
    return v[0] && v[0]!=~0u && ((v[4]>>8)&255)==3 &&
        !(v[1]&0x1001) && !v[3] && tx_depth>=2 && rx_depth>=2 &&
        tx_width==32 && rx_width==32;
}
#endif
