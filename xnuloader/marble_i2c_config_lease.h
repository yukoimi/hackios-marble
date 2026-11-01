/* GENI FIFO/100kHz setup derived from pinned Linux qcom-geni-se.c and
 * i2c-qcom-geni.c. Configuration-only lease: no commands/reset/amp writes. */
#ifndef MARBLE_I2C_CONFIG_LEASE_H
#define MARBLE_I2C_CONFIG_LEASE_H
#include <stdint.h>
#define MARBLE_I2C_CONFIG_WORDS 13
static const uint64_t marble_i2c_config_addresses[MARBLE_I2C_CONFIG_WORDS]={
    0x880e1c,0x880e18,0x88007c,0x880048,0x880278,0x880254,
    0x880260,0x880264,0x880284,0x880288,0x880258,0xf138000,0xf139000
};
static const uint32_t marble_i2c_config_values[MARBLE_I2C_CONFIG_WORDS]={
    0,0,0,0x71,0xa02c1a,0,0x7f8fe,0xffefe,0x7f8fe,0xffefe,0,7,7
};
typedef struct {
    void *ctx;
    int (*read)(void *,uint64_t,uint32_t *);
    int (*write)(void *,uint64_t,uint32_t);
} MarbleI2cConfigOps;
typedef struct {
    uint32_t saved[MARBLE_I2C_CONFIG_WORDS];
    unsigned touched,prepared,active,dirty;
} MarbleI2cConfigLease;
static int marble_i2c_config_allowed(uint64_t address)
{
    for(unsigned i=0;i<MARBLE_I2C_CONFIG_WORDS;i++)if(address==marble_i2c_config_addresses[i])return 1;
    return 0;
}
static int marble_i2c_config_prepare(const MarbleI2cConfigOps *o,MarbleI2cConfigLease *p)
{
    if(p->prepared || p->touched || p->active || p->dirty)return 0;
    for(unsigned i=0;i<MARBLE_I2C_CONFIG_WORDS;i++)if(o->read(o->ctx,marble_i2c_config_addresses[i],&p->saved[i]))return 0;
    p->prepared=1;return 1;
}
static int marble_i2c_config_restore(const MarbleI2cConfigOps *o,MarbleI2cConfigLease *p)
{
    if(!p->prepared)return 0;
    int ok=1;
    for(int i=MARBLE_I2C_CONFIG_WORDS-1;i>=0;i--)if(p->touched&(1u<<i)) {
        uint32_t v=~p->saved[i];
        if(o->write(o->ctx,marble_i2c_config_addresses[i],p->saved[i]) ||
           o->read(o->ctx,marble_i2c_config_addresses[i],&v) || v!=p->saved[i])ok=0;
        else p->touched&=~(1u<<i);
    }
    for(unsigned i=0;i<MARBLE_I2C_CONFIG_WORDS;i++) {
        uint32_t v=~p->saved[i];
        if(o->read(o->ctx,marble_i2c_config_addresses[i],&v) || v!=p->saved[i])ok=0;
    }
    p->active=p->prepared=0;p->dirty=!ok || !!p->touched;return !p->dirty;
}
static int marble_i2c_config_apply(const MarbleI2cConfigOps *o,MarbleI2cConfigLease *p)
{
    if(!p->prepared || p->active || p->touched || p->dirty)return 0;
    for(unsigned i=0;i<MARBLE_I2C_CONFIG_WORDS;i++) {
        uint32_t v=~p->saved[i];
        if(o->read(o->ctx,marble_i2c_config_addresses[i],&v) || v!=p->saved[i])return 0;
    }
    for(unsigned i=0;i<MARBLE_I2C_CONFIG_WORDS;i++)if(p->saved[i]!=marble_i2c_config_values[i]) {
        uint32_t v=~marble_i2c_config_values[i];
        /* The callback may apply the write before returning an error. This
         * is our exclusive, snapshotted configuration word, so restore it. */
        p->touched|=1u<<i;
        if(o->write(o->ctx,marble_i2c_config_addresses[i],marble_i2c_config_values[i]) ||
           o->read(o->ctx,marble_i2c_config_addresses[i],&v) || v!=marble_i2c_config_values[i]) {
            marble_i2c_config_restore(o,p);return 0;
        }
    }
    p->active=1;return 1;
}
#endif
