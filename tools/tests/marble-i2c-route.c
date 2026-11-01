#include <assert.h>
#include <stdio.h>
#include "../../xnuloader/marble_i2c_route.h"
int main(void)
{
    uint32_t a[41]={0},b[41]={0};assert(marble_i2c_route_stable(a,b));
    for(unsigned i=0;i<41;i++) {
        uint64_t addr=marble_i2c_route_addresses[i];assert(marble_i2c_route_allowed(addr));
        assert(!marble_i2c_route_allowed(addr+1));assert(!marble_i2c_route_allowed(addr-1));
        for(unsigned j=0;j<i;j++)assert(addr!=marble_i2c_route_addresses[j]);
        b[i]=2;assert(!marble_i2c_route_stable(a,b));b[i]=0;
        b[i]=1;assert(marble_i2c_route_stable(a,b)==(i>=31 && !(i&1)));b[i]=0;
    }
    assert(!marble_i2c_route_allowed(0x880700));assert(!marble_i2c_route_allowed(0x880780));
    assert(!marble_i2c_route_allowed(0x880618));assert(!marble_i2c_route_allowed(0x880604));
    puts("PASS route snapshot: exact 41 addresses; excludes FIFO data/IRQ clear/command-control; preserves every configuration and GPIO output while ignoring independent GPIO input");
}
