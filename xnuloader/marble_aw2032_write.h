/* Mechanism only, matching read_register_raw: the per-operation callback
 * must independently authorize the register/value. Public policies remain
 * in profile/quiet/digital wrappers; no hardware access occurs here itself. */
#ifndef MARBLE_AW2032_WRITE_H
#define MARBLE_AW2032_WRITE_H
#include "marble_i2c_fifo.h"
static uint32_t marble_aw2032_register_word(unsigned reg,uint16_t value)
{return reg|((uint32_t)(value>>8)<<8)|((uint32_t)(value&255)<<16);}
static int marble_aw2032_write_raw(const MarbleI2cFifoOps *o,unsigned address,unsigned reg,uint16_t value,MarbleI2cFifoResult *p)
{
    if(!o || !p || !o->read || !o->write || !o->now_us || !o->delay_us || p->attempted ||
       reg>=0x7e || (address!=0x34 && address!=0x35))return 0;
    p->attempted=1;if(!marble_i2c_fifo_clean(o))return 0;
    uint64_t start=o->now_us(o->ctx);int ok=0;
    if(o->write(o->ctx,MIF_TX_LEN,3))goto cleanup;
    p->commands++;
    if(o->write(o->ctx,MIF_CMD,(1u<<27)|(address<<9)) || o->write(o->ctx,MIF_TX_WATERMARK,1) ||
       !marble_i2c_fifo_wait_word(o,p,0,marble_aw2032_register_word(reg,value)))goto cleanup;
    ok=marble_i2c_fifo_idle(o,&p->idle_irq,&p->idle_irq_enable);
cleanup:
    if(!ok && p->commands && !marble_i2c_fifo_abort(o,p))p->dirty=1;
    p->elapsed_us=o->now_us(o->ctx)-start;p->done=ok;return ok;
}
#endif
