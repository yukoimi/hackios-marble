#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../xnu/files/bsd/dev/marble_gpu/marble_gpu_power.h"
struct device {uint32_t regs[0xa000/4];unsigned writes,us,observes;int power_timeout,clock_timeout,observe_fail;};
static uint32_t read_reg(void *cookie,unsigned offset)
{struct device *d=cookie;assert(offset<0xa000 && !(offset&3));return d->regs[offset/4];}
static void write_reg(void *cookie,unsigned offset,uint32_t v)
{
    struct device *d=cookie;
    assert(offset==MGP_CX || offset==MGP_AHB || offset==MGP_SMMU);
    d->writes++;
    if(offset==MGP_CX) {
        d->regs[offset/4]=v&0x7fffffffu;
        d->regs[MGP_CX_STATUS/4]=(!(v&1) && !d->power_timeout)?0x80000000u:0;
    } else if(offset==MGP_SMMU) {
        assert(d->regs[MGP_CX_STATUS/4]&0x80000000u);
        d->regs[offset/4]=(v&0x7fffffffu)|((!(v&1)||d->clock_timeout)?0x80000000u:0);
    } else d->regs[offset/4]=v;
}
static void delay_us(void *cookie,unsigned us)
{struct device *d=cookie;d->us+=us;assert(d->us<2000);}
static int observe(void *cookie)
{
    struct device *d=cookie;d->observes++;
    assert(d->regs[MGP_CX_STATUS/4]&0x80000000u);
    assert(d->regs[MGP_AHB/4]&1);
    assert(!(d->regs[MGP_SMMU/4]&0x80000000u));
    return d->observe_fail?-1:0;
}
static void initial(struct device *d)
{memset(d,0,sizeof(*d));d->regs[MGP_CX/4]=d->regs[MGP_GX/4]=0x22f001;d->regs[MGP_SMMU/4]=0x80000000u;}
int main(void)
{
    struct device d;struct marble_gpu_power_result r;
    struct marble_gpu_power_io io={&d,read_reg,write_reg,delay_us,observe};
    initial(&d);marble_gpu_power_transaction(&io,&r);
    assert(r.cx_on && r.clocks_on && r.observed && r.restored && d.observes==1);
    assert(d.regs[MGP_CX/4]==0x22f001 && d.regs[MGP_SMMU/4]==0x80000000u);
    initial(&d);d.power_timeout=1;marble_gpu_power_transaction(&io,&r);
    assert(!r.cx_on && !r.observed && r.restored && d.writes==2 && !d.observes);
    initial(&d);d.clock_timeout=1;marble_gpu_power_transaction(&io,&r);
    assert(r.cx_on && !r.clocks_on && !r.observed && r.restored && !d.observes);
    initial(&d);d.observe_fail=1;marble_gpu_power_transaction(&io,&r);
    assert(r.cx_on && r.clocks_on && !r.observed && r.restored && d.observes==1);
    initial(&d);d.regs[MGP_CX/4]|=2;marble_gpu_power_transaction(&io,&r);
    assert(r.skipped && !d.writes && !d.observes);
    initial(&d);d.regs[MGP_CX_STATUS/4]=0x80000000u;marble_gpu_power_transaction(&io,&r);
    assert(r.skipped && !d.writes && !d.observes);
    puts("marble CX transaction: restore on success, power/clock timeout, observation failure; refuse hardware ownership PASS");
    return 0;
}
