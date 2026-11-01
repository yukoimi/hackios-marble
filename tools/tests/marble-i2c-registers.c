#include <assert.h>
#include <stdio.h>
#include "../../xnuloader/marble_i2c_registers.h"
int main(void)
{
    uint32_t v[8]={0x10010000,0,0,0,0x300,0x300,0x20100000,0x20100000};
    assert(marble_i2c_ro_identity(v));
    for(uint64_t a=0x880000;a<=0x8c1000;a++) {
        unsigned expected=0;for(unsigned i=0;i<8;i++)expected|=a==marble_i2c_ro_addresses[i];
        assert(marble_i2c_ro_address_allowed(a)==(int)expected);
    }
    for(unsigned i=0;i<8;i++) {
        uint32_t saved=v[i];v[i]=~0u;assert(!marble_i2c_ro_identity(v) || i==2 || i==5);v[i]=saved;
    }
    v[1]=1;assert(!marble_i2c_ro_identity(v));v[1]=0x1000;assert(!marble_i2c_ro_identity(v));v[1]=0;
    v[3]=1;assert(!marble_i2c_ro_identity(v));v[3]=0;v[4]=0x100;assert(!marble_i2c_ro_identity(v));v[4]=0x300;
    v[6]=0x20010000;assert(!marble_i2c_ro_identity(v));v[6]=0x10100000;assert(!marble_i2c_ro_identity(v));
    puts("PASS passive I2C inventory: exact eight addresses only; rejects active command/non-I2C/FIFO disabled/invalid FIFO geometry; no hardware calls");
}
