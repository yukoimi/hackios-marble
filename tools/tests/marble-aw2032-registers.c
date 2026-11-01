#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../xnuloader/marble_aw2032_registers.h"
typedef struct {unsigned reg;uint16_t value;unsigned address,calls,fail_at,stage,sent,consumed,stalled,nack,bad_count,abort_stuck,commands,aborts,watermark,rx_watermark,receiver_started,enable_summary;uint32_t irq,s_irq;uint64_t us;} Model;
static int read_word(void *ctx,uint64_t a,uint32_t *v)
{
    Model *m=ctx;assert(marble_i2c_fifo_read_allowed(a));if(++m->calls==m->fail_at)return 1;
    if(a==MIF_STATUS)*v=m->stage && (m->stalled || m->abort_stuck)?1:0;
    else if(a==MIF_IRQ) {
        if(m->stalled)*v=0;
        else if(m->nack && m->stage)*v=1u<<10;
        else if(m->irq || m->s_irq || (!m->rx_watermark && m->receiver_started))*v=m->irq|((m->s_irq || (!m->rx_watermark && m->receiver_started))?0x80:0);
        else *v=m->watermark && !m->sent?1u<<30:0;
    }
    else if(a==MIF_S_IRQ)*v=m->s_irq;
    else if(a==MIF_IRQ_ENABLE)*v=0x7fc0347f|(m->enable_summary?0x80:0);
    else if(a==MIF_TX_STATUS)*v=0;
    else if(a==MIF_RX_STATUS)*v=m->stage==2 && !m->consumed && !m->stalled?0xa0000001u+(m->bad_count?1:0):0;
    else {assert(a==MIF_RX && m->stage==2 && !m->consumed);*v=((m->value&255)<<8)|(m->value>>8);m->consumed=1;}
    return 0;
}
static int write_word(void *ctx,uint64_t a,uint32_t v)
{
    Model *m=ctx;assert(marble_aw2032_read_write_allowed(a,v,m->address,m->reg));
    if(a==MIF_CMD){m->stage=v>>27;m->commands++;m->irq=m->stage==2?0x8000001u:0;if(m->stage==2){m->s_irq=0xc000000;m->receiver_started=1;}}
    else if(a==MIF_TX){assert(m->stage==1 && !m->sent && v==m->reg);m->sent=1;m->irq=1;}
    else if(a==MIF_CLEAR){m->irq&=~v;if(v&1)m->stage=0;}
    else if(a==MIF_TX_WATERMARK)m->watermark=v;
    else if(a==MIF_S_CLEAR)m->s_irq&=~v;
    else if(a==MIF_CTRL){m->aborts++;if(!m->abort_stuck){m->stage=0;m->irq=0;m->s_irq=0;m->stalled=0;m->nack=0;m->consumed=1;}}
    return ++m->calls==m->fail_at;
}
static uint64_t now(void *ctx){return ((Model *)ctx)->us;}
static void delay(void *ctx,unsigned us){((Model *)ctx)->us+=us;}
int main(void)
{
    for(unsigned address=0x34;address<=0x35;address++)for(unsigned i=0;i<MARBLE_AW2032_SNAPSHOT_WORDS;i++) {
        unsigned reg=marble_aw2032_snapshot_registers[i];
        Model m={.address=address,.reg=reg,.value=0x5ac3,.rx_watermark=13};
        MarbleI2cFifoOps o={&m,read_word,write_word,now,delay};MarbleI2cFifoResult p={0};
        assert(marble_aw2032_read_register(&o,address,reg,&p));
        assert(p.done && p.chip_id==0x5ac3 && p.commands==2 && p.tx_words==1 && p.rx_words==1 && !p.dirty && !p.aborts);
        assert(!marble_aw2032_read_register(&o,address,reg,&p));unsigned calls=m.calls;
        for(unsigned fail=1;fail<=calls;fail++) {
            m=(Model){.address=address,.reg=reg,.value=0x5ac3,.rx_watermark=13,.fail_at=fail};p=(MarbleI2cFifoResult){0};
            assert(!marble_aw2032_read_register(&o,address,reg,&p));assert(p.commands<=2 && m.commands<=2 && p.tx_words<=1 && p.rx_words<=1 && m.us<=30000);
        }
        for(unsigned mode=0;mode<4;mode++) {
            m=(Model){.address=address,.reg=reg,.rx_watermark=13,.stalled=mode==0 || mode==3,.nack=mode==1,.bad_count=mode==2,.abort_stuck=mode==3};p=(MarbleI2cFifoResult){0};
            assert(!marble_aw2032_read_register(&o,address,reg,&p));assert(p.aborts==1 && p.dirty==(mode==3) && m.us<=30000);
        }
        m=(Model){.address=address,.reg=reg,.value=0x1234};p=(MarbleI2cFifoResult){0};
        assert(marble_aw2032_read_register(&o,address,reg,&p));assert(p.chip_id==0x1234 && p.idle_irq==0x80 && !(p.idle_irq_enable&0x80));
        m=(Model){.address=address,.reg=reg,.enable_summary=1};p=(MarbleI2cFifoResult){0};
        assert(!marble_aw2032_read_register(&o,address,reg,&p));assert(p.dirty);
    }
    Model m={0};MarbleI2cFifoOps o={&m,read_word,write_word,now,delay};MarbleI2cFifoResult p={0};
    for(unsigned reg=0;reg<256;reg++)if(!marble_aw2032_readable(reg)) {
        assert(!marble_aw2032_read_register(&o,0x34,reg,&p));assert(!m.calls && !p.attempted);
        assert(!marble_aw2032_read_write_allowed(MIF_TX,reg,0x34,reg));
    }
    assert(!marble_aw2032_read_register(&o,0x36,4,&p));assert(!m.calls);
    assert(!marble_aw2032_read_write_allowed(MIF_TX,0x400304,0x34,4));
    assert(!marble_aw2032_read_write_allowed(MIF_TX_LEN,3,0x34,4));
    assert(!marble_aw2032_read_write_allowed(MIF_TX,5,0x34,4));
    assert(!marble_i2c_fifo_write_allowed(MIF_TX,4,0x34));
    puts("PASS AW2032 read-only registers: both devices, explicit non-destructive register list, endian conversion, all callback failures, NACK, timeout, stuck abort and masked IRQ; legacy ID-only gate unchanged");
}
