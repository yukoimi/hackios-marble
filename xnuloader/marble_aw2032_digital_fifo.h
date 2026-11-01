#ifndef MARBLE_AW2032_DIGITAL_FIFO_H
#define MARBLE_AW2032_DIGITAL_FIFO_H
#include "marble_aw2032_digital.h"
#include "marble_aw2032_registers.h"
#include "marble_aw2032_write.h"
static int marble_aw2032_digital_fifo_allowed(uint64_t a,uint32_t v,unsigned address,unsigned reg,uint16_t value,unsigned readback)
{
    if(readback>1 || (address!=0x34 && address!=0x35))return 0;
    if(readback)return (reg==1 || reg==4 || reg==5) && marble_aw2032_read_write_allowed(a,v,address,reg);
    if(!marble_aw2032_digital_allowed(address,reg,value))return 0;
    if(a==MIF_TX_LEN)return v==3;
    if(a==MIF_TX)return v==marble_aw2032_register_word(reg,value);
    if(a==MIF_CMD)return v==((1u<<27)|(address<<9));
    return marble_i2c_fifo_write_allowed(a,v,address);
}
static int marble_aw2032_digital_write(const MarbleI2cFifoOps *o,unsigned address,unsigned reg,uint16_t value,MarbleI2cFifoResult *p)
{
    return marble_aw2032_digital_allowed(address,reg,value) && marble_aw2032_write_raw(o,address,reg,value,p);
}
#endif
