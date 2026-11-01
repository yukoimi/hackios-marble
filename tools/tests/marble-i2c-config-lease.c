#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../xnuloader/marble_i2c_config_lease.h"
typedef struct {uint32_t values[13],initial[13];unsigned calls,fail_at,writes,corrupt;int restoring,order;} Model;
static unsigned index_of(uint64_t address)
{
    assert(marble_i2c_config_allowed(address));
    for(unsigned i=0;i<13;i++)if(marble_i2c_config_addresses[i]==address)return i;
    assert(0);return 0;
}
static int read_word(void *ctx,uint64_t address,uint32_t *v)
{
    Model *m=ctx;if(++m->calls==m->fail_at)return 1;*v=m->values[index_of(address)];return 0;
}
static int write_word(void *ctx,uint64_t address,uint32_t v)
{
    Model *m=ctx;unsigned i=index_of(address);m->writes++;
    if(m->restoring){assert((int)i<m->order);m->order=(int)i;}
    m->values[i]=v^m->corrupt;return ++m->calls==m->fail_at;
}
static Model initial(unsigned variant)
{
    Model m={0};m.order=13;
    for(unsigned i=0;i<13;i++)m.values[i]=m.initial[i]=variant?0x12345678+i:marble_i2c_config_values[i];
    return m;
}
int main(void)
{
    for(unsigned variant=0;variant<2;variant++) {
        Model m=initial(variant);MarbleI2cConfigOps o={&m,read_word,write_word};MarbleI2cConfigLease p={0};
        assert(!marble_i2c_config_apply(&o,&p));assert(!marble_i2c_config_restore(&o,&p));
        assert(marble_i2c_config_prepare(&o,&p));assert(!marble_i2c_config_prepare(&o,&p));
        assert(marble_i2c_config_apply(&o,&p));assert(!marble_i2c_config_apply(&o,&p));
        for(unsigned i=0;i<13;i++)assert(m.values[i]==marble_i2c_config_values[i]);
        m.restoring=1;assert(marble_i2c_config_restore(&o,&p));assert(!memcmp(m.values,m.initial,sizeof(m.values)));assert(!p.dirty);
        unsigned calls=m.calls;
        for(unsigned failure=1;failure<=calls;failure++) {
            m=initial(variant);m.fail_at=failure;memset(&p,0,sizeof(p));
            if(marble_i2c_config_prepare(&o,&p)) {
                if(marble_i2c_config_apply(&o,&p))assert(!marble_i2c_config_restore(&o,&p));
                else if(p.prepared)assert(marble_i2c_config_restore(&o,&p));
            }
            if(!p.dirty)assert(!memcmp(m.values,m.initial,sizeof(m.values)));
            else assert(!marble_i2c_config_prepare(&o,&p));
        }
    }
    Model m=initial(1);MarbleI2cConfigOps o={&m,read_word,write_word};MarbleI2cConfigLease p={0};
    assert(marble_i2c_config_prepare(&o,&p));m.values[3]^=1;assert(!marble_i2c_config_apply(&o,&p));assert(!m.writes);assert(!marble_i2c_config_restore(&o,&p));
    assert(!marble_i2c_config_allowed(0x880600) && !marble_i2c_config_allowed(0x880700) && !marble_i2c_config_allowed(0xf101004));
    m=initial(1);memset(&p,0,sizeof(p));assert(marble_i2c_config_prepare(&o,&p));m.corrupt=1;assert(!marble_i2c_config_apply(&o,&p));assert(p.dirty);
    puts("PASS 13-word configuration lease: no-op and changed snapshots, every call fault including applied-then-error write, reverse restoration, foreign drift and readback corruption; excludes commands/reset/FIFO");
}
