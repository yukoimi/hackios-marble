/* Only permits the OEM AW2032 90dB attenuation + HMUTE write to SYSCTRL2.
 * Requires observed PWDN/AMPPD and HMUTE. No enable, boost, OTP or gain-up.
 * The reset lease owner must assert hardware reset even on transfer failure. */
#ifndef MARBLE_AW2032_QUIET_H
#define MARBLE_AW2032_QUIET_H
#include "marble_aw2032_registers.h"
static uint16_t marble_aw2032_quiet_value(uint16_t before)
{
    /* OEM value=90*8, step=6*8: ((value/48)<<6 | value%48)<<6.
     * Keep low six control bits; enforce HMUTE. 90dB is representable. */
    return (uint16_t)(0xf000u|(before&0x3fu)|0x10u);
}
static uint32_t marble_aw2032_quiet_word(uint16_t before)
{
    uint16_t v=marble_aw2032_quiet_value(before);
    /* GENI FIFO is byte-packed LSB first; AW value is MSB first on wire. */
    return 5u|((uint32_t)(v>>8)<<8)|((uint32_t)(v&255)<<16);
}
static int marble_aw2032_quiet_write_allowed(uint64_t a,uint32_t v,unsigned address,uint16_t before)
{
    if((address!=0x34 && address!=0x35) || !(before&0x10))return 0;
    if(a==MIF_TX_LEN)return v==3;
    if(a==MIF_TX)return v==marble_aw2032_quiet_word(before);
    if(a==MIF_CMD)return v==((1u<<27)|(address<<9)); /* write + STOP */
    if(a==MIF_RX_LEN)return 0;
    return marble_i2c_fifo_write_allowed(a,v,address);
}
static int marble_aw2032_quiet_write(const MarbleI2cFifoOps *o,unsigned address,uint16_t sysctrl,uint16_t before,MarbleI2cFifoResult *p)
{
    if(!o || !p || !o->read || !o->write || !o->now_us || !o->delay_us ||
       p->attempted || (address!=0x34 && address!=0x35) || (sysctrl&3)!=3 || !(before&0x10))return 0;
    p->attempted=1;
    if(!marble_i2c_fifo_clean(o))return 0;
    uint64_t start=o->now_us(o->ctx);int ok=0;
    if(o->write(o->ctx,MIF_TX_LEN,3))goto cleanup;
    p->commands++;
    if(o->write(o->ctx,MIF_CMD,(1u<<27)|(address<<9)) || o->write(o->ctx,MIF_TX_WATERMARK,1) ||
       !marble_i2c_fifo_wait_word(o,p,0,marble_aw2032_quiet_word(before)))goto cleanup;
    ok=marble_i2c_fifo_idle(o,&p->idle_irq,&p->idle_irq_enable);
cleanup:
    if(!ok && p->commands && !marble_i2c_fifo_abort(o,p))p->dirty=1;
    p->elapsed_us=o->now_us(o->ctx)-start;p->done=ok;return ok;
}
#endif
