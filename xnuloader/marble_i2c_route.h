/* Passive GENI/GCC/TLMM observations. No FIFO data/clear or register writes.
 * Pinned Xiaomi msm-geni-se.h, gcc-waipio.c, pinctrl-waipio.h; marble DT. */
#ifndef MARBLE_I2C_ROUTE_H
#define MARBLE_I2C_ROUTE_H
#include <stdint.h>
#define MARBLE_I2C_ROUTE_WORDS 41
static const uint64_t marble_i2c_route_addresses[MARBLE_I2C_ROUTE_WORDS]={
    0x880048,0x88004c,0x88007c,0x880258,0x880260,0x880264,0x88026c,0x880270,
    0x880278,0x880284,0x880288,0x880600,0x880610,0x880614,0x880630,0x880640,
    0x880644,0x880800,0x880804,0x88080c,0x880810,0x880814,0x880908,0x880e18,0x880e1c,
    0x12e018,0x12e01c,0x12e020,0x12e024,0x12e008,0x162010,
    0xf138000,0xf138004,0xf139000,0xf139004,
    0xf101000,0xf101004,0xf178000,0xf178004,0xf134000,0xf134004
};
static int marble_i2c_route_allowed(uint64_t address)
{
    for(unsigned i=0;i<MARBLE_I2C_ROUTE_WORDS;i++)if(address==marble_i2c_route_addresses[i])return 1;
    return 0;
}
static unsigned marble_i2c_route_preserve_mask(unsigned i)
{
    /* GPIO input levels can change independently; preserve output latch. */
    return i>=31 && !(i&1)?2u:~0u;
}
static int marble_i2c_route_stable(const uint32_t a[MARBLE_I2C_ROUTE_WORDS],const uint32_t b[MARBLE_I2C_ROUTE_WORDS])
{
    for(unsigned i=0;i<MARBLE_I2C_ROUTE_WORDS;i++)
        if((a[i]^b[i])&marble_i2c_route_preserve_mask(i))return 0;
    return 1;
}
#endif
