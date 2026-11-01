/* Bounded 1-byte register pointer + repeated-start 2-byte ID read.
 * Linux i2c-qcom-geni.c I2C_WRITE/READ and STOP_STRETCH; aw882xx.c reads
 * register zero and interprets the two bytes MSB first. No amp data writes.
 * Caller owns clocks, pin/config/reset lifecycle and interrupt forwarding. */
#ifndef MARBLE_I2C_FIFO_H
#define MARBLE_I2C_FIFO_H
#include <stdint.h>
#define MIF_STATUS 0x880040ULL
#define MIF_TX_LEN 0x88026cULL
#define MIF_RX_LEN 0x880270ULL
#define MIF_CMD 0x880600ULL
#define MIF_CTRL 0x880604ULL
#define MIF_IRQ 0x880610ULL
#define MIF_IRQ_ENABLE 0x880614ULL
#define MIF_CLEAR 0x880618ULL
#define MIF_S_IRQ 0x880640ULL
#define MIF_S_CLEAR 0x880648ULL
#define MIF_TX 0x880700ULL
#define MIF_RX 0x880780ULL
#define MIF_TX_STATUS 0x880800ULL
#define MIF_RX_STATUS 0x880804ULL
#define MIF_TX_WATERMARK 0x88080cULL
#define MIF_ERR 0x3300340eu
#define MIF_KNOWN_IRQ 0xffc0ffffu
typedef struct {
    void *ctx;
    int (*read)(void *,uint64_t,uint32_t *);
    int (*write)(void *,uint64_t,uint32_t);
    uint64_t (*now_us)(void *);
    void (*delay_us)(void *,unsigned);
} MarbleI2cFifoOps;
typedef struct {
    unsigned attempted,commands,tx_words,rx_words,polls,aborts,dirty,done;
    uint32_t observed_irq,last_irq,raw_rx;
    uint32_t observed_s_irq,last_s_irq;
    uint32_t idle_irq,idle_irq_enable;
    uint64_t elapsed_us;
    uint16_t chip_id;
} MarbleI2cFifoResult;
static int marble_i2c_fifo_read_allowed(uint64_t a)
{
    return a==MIF_STATUS || a==MIF_IRQ || a==MIF_IRQ_ENABLE || a==MIF_S_IRQ || a==MIF_TX_STATUS || a==MIF_RX_STATUS || a==MIF_RX;
}
static int marble_i2c_fifo_write_allowed(uint64_t a,uint32_t v,unsigned address)
{
    if(address!=0x34 && address!=0x35)return 0;
    if(a==MIF_TX_LEN)return v==1;
    if(a==MIF_RX_LEN)return v==2;
    if(a==MIF_TX_WATERMARK)return v<=1;
    if(a==MIF_TX)return !v; /* register address zero only; no register value */
    if(a==MIF_CMD)return v==((1u<<27)|(address<<9)|4u) || v==((2u<<27)|(address<<9));
    if(a==MIF_CTRL)return v==2;
    if(a==MIF_CLEAR)return v && !(v&~MIF_KNOWN_IRQ);
    if(a==MIF_S_CLEAR)return v && !(v&~0x0c000000u);
    return 0;
}
static int marble_i2c_fifo_clear(const MarbleI2cFifoOps *o,MarbleI2cFifoResult *p,uint32_t irq)
{
    p->last_irq=irq;p->observed_irq|=irq;
    return !irq || (!(irq&~MIF_KNOWN_IRQ) && !o->write(o->ctx,MIF_CLEAR,irq));
}
static int marble_i2c_fifo_idle(const MarbleI2cFifoOps *o,uint32_t *raw,uint32_t *enabled)
{
    uint32_t status=~0u,irq=~0u,enable=~0u,secondary=~0u,tx=~0u,rx=~0u;
    int ok=!o->read(o->ctx,MIF_STATUS,&status) && !(status&0x1001) &&
        !o->read(o->ctx,MIF_IRQ,&irq) && !o->read(o->ctx,MIF_IRQ_ENABLE,&enable) &&
        !(irq&~0x80u) && !(irq&enable) && !o->read(o->ctx,MIF_TX_STATUS,&tx) &&
        !(tx&0x0fffffff) && !o->read(o->ctx,MIF_RX_STATUS,&rx) && !(rx&0x1ffffff) &&
        !o->read(o->ctx,MIF_S_IRQ,&secondary) && !secondary;
    /* Raw M_RX may remain asserted after the RX FIFO is empty, even with
     * its secondary events acknowledged. It is not an enabled interrupt:
     * accept only this one explicitly masked bit, require cold reset later,
     * and never pretend that the raw status word was restored to zero. */
    if(raw)*raw=irq;if(enabled)*enabled=enable;return ok;
}
static int marble_i2c_fifo_clean(const MarbleI2cFifoOps *o)
{return marble_i2c_fifo_idle(o,0,0);}
static int marble_i2c_fifo_rx_ack(const MarbleI2cFifoOps *o,MarbleI2cFifoResult *p)
{
    uint32_t irq=0;if(o->read(o->ctx,MIF_S_IRQ,&irq))return 0;
    p->last_s_irq=irq;p->observed_s_irq|=irq;
    /* Initial secondary state was zero, and our only secondary source is
     * the single owned RX word. Never clear unrelated secondary events. */
    if(irq&~0x0c000000u || (irq && !p->rx_words))return 0;
    return !irq || !o->write(o->ctx,MIF_S_CLEAR,irq);
}
/* Shared bounded FIFO mechanism; callers separately authorize the TX word.
 * The public ID path below continues to authorize only register pointer zero. */
static int marble_i2c_fifo_wait_word(const MarbleI2cFifoOps *o,MarbleI2cFifoResult *p,int receive,uint32_t word)
{
    uint64_t start=o->now_us(o->ctx);
    /* Both elapsed-time and iteration bounds apply even to a broken clock. */
    for(unsigned poll=0;poll<4000 && o->now_us(o->ctx)-start<20000;poll++) {
        uint32_t irq=0,rx=0;p->polls++;
        if(o->read(o->ctx,MIF_IRQ,&irq))return 0;
        p->last_irq=irq;p->observed_irq|=irq;
        if(irq&MIF_ERR || irq&~MIF_KNOWN_IRQ)return 0;
        if(!receive && irq&(1u<<30) && !p->tx_words) {
            if(o->write(o->ctx,MIF_TX,word))return 0;p->tx_words++;
            if(o->write(o->ctx,MIF_TX_WATERMARK,0))return 0;
        }
        if(receive) {
            if(o->read(o->ctx,MIF_RX_STATUS,&rx))return 0;
            unsigned words=rx&0x1ffffff;
            if(words>1 || (words && p->rx_words))return 0;
            if(words) {
                if(o->read(o->ctx,MIF_RX,&p->raw_rx))return 0;p->rx_words++;
                /* Last-word valid-byte count is zero for a full word. A
                 * 2-byte request must explicitly report exactly two bytes. */
                if(!(rx&0x80000000u) || (rx>>28&7)!=2)return 0;
                if(!marble_i2c_fifo_rx_ack(o,p))return 0;
            }
        }
        if(!marble_i2c_fifo_clear(o,p,irq))return 0;
        if(irq&1) {
            if(receive)o->delay_us(o->ctx,5); /* summary deassert propagation */
            return receive?p->rx_words==1:p->tx_words==1;
        }
        o->delay_us(o->ctx,5);
    }
    return 0;
}
static int marble_i2c_fifo_wait(const MarbleI2cFifoOps *o,MarbleI2cFifoResult *p,int receive)
{return marble_i2c_fifo_wait_word(o,p,receive,0);}
static int marble_i2c_fifo_abort(const MarbleI2cFifoOps *o,MarbleI2cFifoResult *p)
{
    uint32_t status=~0u;uint64_t start=o->now_us(o->ctx);p->aborts++;
    int watermark_off=!o->write(o->ctx,MIF_TX_WATERMARK,0);
    if(o->write(o->ctx,MIF_CTRL,2))return 0;
    for(unsigned poll=0;poll<2000 && o->now_us(o->ctx)-start<10000;poll++) {
        uint32_t irq=0;
        if(o->read(o->ctx,MIF_IRQ,&irq) || o->read(o->ctx,MIF_STATUS,&status))return 0;
        if(p->rx_words && !marble_i2c_fifo_rx_ack(o,p))return 0;
        if(!marble_i2c_fifo_clear(o,p,irq))return 0;
        if(!(status&0x1001))return watermark_off && marble_i2c_fifo_clean(o);
        o->delay_us(o->ctx,5);
    }
    return 0;
}
static int marble_i2c_fifo_id(const MarbleI2cFifoOps *o,unsigned address,MarbleI2cFifoResult *p)
{
    if(p->attempted || (address!=0x34 && address!=0x35))return 0;
    p->attempted=1;
    if(!marble_i2c_fifo_clean(o))return 0;
    uint64_t start=o->now_us(o->ctx);int ok=0;
    if(o->write(o->ctx,MIF_TX_LEN,1))goto cleanup;
    /* Account for a command that might be submitted before callback error. */
    p->commands++;
    if(o->write(o->ctx,MIF_CMD,(1u<<27)|(address<<9)|4u) ||
       o->write(o->ctx,MIF_TX_WATERMARK,1) || !marble_i2c_fifo_wait(o,p,0))goto cleanup;
    if(o->write(o->ctx,MIF_RX_LEN,2))goto cleanup;
    p->commands++;
    if(o->write(o->ctx,MIF_CMD,(2u<<27)|(address<<9)) || !marble_i2c_fifo_wait(o,p,1))goto cleanup;
    p->chip_id=(uint16_t)(((p->raw_rx&255)<<8)|((p->raw_rx>>8)&255));
    ok=marble_i2c_fifo_idle(o,&p->idle_irq,&p->idle_irq_enable);
cleanup:
    if(!ok && p->commands && !marble_i2c_fifo_abort(o,p))p->dirty=1;
    p->elapsed_us=o->now_us(o->ctx)-start;p->done=ok;
    return ok;
}
#endif
