/* AW2032 register reads, separate from the audited ID-only API. Register
 * pointers are one byte; values are big endian, as in the pinned OEM driver.
 * No SYSINT read (clear-on-read), test/OTP programming, or amplifier writes. */
#ifndef MARBLE_AW2032_REGISTERS_H
#define MARBLE_AW2032_REGISTERS_H
#include "marble_i2c_fifo.h"
#define MARBLE_AW2032_SNAPSHOT_WORDS 10u
static const unsigned marble_aw2032_snapshot_registers[MARBLE_AW2032_SNAPSHOT_WORDS]={1,3,4,5,6,7,8,0x66,0x78,0x7a};
static int marble_aw2032_readable(unsigned reg)
{
    for(unsigned i=0;i<MARBLE_AW2032_SNAPSHOT_WORDS;i++)
        if(reg==marble_aw2032_snapshot_registers[i])return 1;
    return 0;
}
static int marble_aw2032_read_write_allowed(uint64_t a,uint32_t v,unsigned address,unsigned reg)
{
    if(!marble_aw2032_readable(reg) || (address!=0x34 && address!=0x35))return 0;
    if(a==MIF_TX)return v==reg;
    return marble_i2c_fifo_write_allowed(a,v,address);
}
/* Mechanism only: a separate per-operation callback must authorize the word.
 * The reset snapshot API below retains its original non-destructive list. */
static int marble_aw2032_read_register_raw(const MarbleI2cFifoOps *o,unsigned address,unsigned reg,MarbleI2cFifoResult *p)
{
    if(!o || !p || !o->read || !o->write || !o->now_us || !o->delay_us ||
       p->attempted || reg>=0x7e || (address!=0x34 && address!=0x35))return 0;
    p->attempted=1;
    if(!marble_i2c_fifo_clean(o))return 0;
    uint64_t start=o->now_us(o->ctx);int ok=0;
    if(o->write(o->ctx,MIF_TX_LEN,1))goto cleanup;
    p->commands++;
    if(o->write(o->ctx,MIF_CMD,(1u<<27)|(address<<9)|4u) ||
       o->write(o->ctx,MIF_TX_WATERMARK,1) || !marble_i2c_fifo_wait_word(o,p,0,reg))goto cleanup;
    if(o->write(o->ctx,MIF_RX_LEN,2))goto cleanup;
    p->commands++;
    if(o->write(o->ctx,MIF_CMD,(2u<<27)|(address<<9)) || !marble_i2c_fifo_wait_word(o,p,1,0))goto cleanup;
    p->chip_id=(uint16_t)(((p->raw_rx&255)<<8)|((p->raw_rx>>8)&255));
    ok=marble_i2c_fifo_idle(o,&p->idle_irq,&p->idle_irq_enable);
cleanup:
    if(!ok && p->commands && !marble_i2c_fifo_abort(o,p))p->dirty=1;
    p->elapsed_us=o->now_us(o->ctx)-start;p->done=ok;return ok;
}
static int marble_aw2032_read_register(const MarbleI2cFifoOps *o,unsigned address,unsigned reg,MarbleI2cFifoResult *p)
{
    return marble_aw2032_readable(reg) && marble_aw2032_read_register_raw(o,address,reg,p);
}
#endif
