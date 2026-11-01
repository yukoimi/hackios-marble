#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../xnuloader/marble_i2c_fifo.h"
typedef struct {unsigned address,calls,fail_at,stage,sent,consumed,stalled,nack,bad_count,abort_stuck,commands,aborts,watermark,rx_watermark,receiver_started,enable_summary;uint32_t irq,s_irq;uint64_t us;} Model;
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
    else {assert(a==MIF_RX && m->stage==2 && !m->consumed);*v=0x3220;m->consumed=1;}
    return 0;
}
static int write_word(void *ctx,uint64_t a,uint32_t v)
{
    Model *m=ctx;assert(marble_i2c_fifo_write_allowed(a,v,m->address));
    if(a==MIF_CMD){m->stage=v>>27;m->commands++;m->irq=m->stage==2?0x8000001u:0;if(m->stage==2){m->s_irq=0xc000000;m->receiver_started=1;}}
    else if(a==MIF_TX){assert(m->stage==1 && !m->sent);m->sent=1;m->irq=1;}
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
    for(unsigned address=0x34;address<=0x35;address++) {
        Model m={.address=address,.rx_watermark=13};MarbleI2cFifoOps o={&m,read_word,write_word,now,delay};MarbleI2cFifoResult p={0};
        assert(marble_i2c_fifo_id(&o,address,&p));assert(p.done && p.chip_id==0x2032 && p.commands==2 && p.tx_words==1 && p.rx_words==1 && !p.dirty && !p.aborts);
        assert(!marble_i2c_fifo_id(&o,address,&p));unsigned calls=m.calls;
        for(unsigned failure=1;failure<=calls;failure++) {
            memset(&m,0,sizeof(m));m.address=address;m.rx_watermark=13;m.fail_at=failure;memset(&p,0,sizeof(p));
            assert(!marble_i2c_fifo_id(&o,address,&p));assert(p.attempted && p.commands<=2 && m.commands<=2 && p.tx_words<=1 && p.rx_words<=1);
            assert(m.us<=30000);
        }
        for(unsigned mode=0;mode<4;mode++) {
            memset(&m,0,sizeof(m));m.address=address;m.rx_watermark=13;m.stalled=mode==0 || mode==3;m.nack=mode==1;m.bad_count=mode==2;m.abort_stuck=mode==3;memset(&p,0,sizeof(p));
            assert(!marble_i2c_fifo_id(&o,address,&p));assert(p.aborts==1 && p.dirty==(mode==3));assert(m.us<=30000);
        }
        memset(&m,0,sizeof(m));m.address=address;memset(&p,0,sizeof(p));
        assert(marble_i2c_fifo_id(&o,address,&p));assert(p.chip_id==0x2032 && !p.aborts && !p.dirty && p.idle_irq==0x80 && !(p.idle_irq_enable&0x80));
        memset(&m,0,sizeof(m));m.address=address;m.enable_summary=1;memset(&p,0,sizeof(p));
        assert(!marble_i2c_fifo_id(&o,address,&p));assert(p.chip_id==0x2032 && p.aborts==1 && p.dirty);
    }
    Model idle={.address=0x34,.watermark=1};MarbleI2cFifoOps o={&idle,read_word,write_word,now,delay};MarbleI2cFifoResult p={0};
    assert(!marble_i2c_fifo_id(&o,0x34,&p));assert(!p.commands && !idle.commands && !p.aborts);
    assert(!marble_i2c_fifo_write_allowed(MIF_TX,1,0x34));assert(!marble_i2c_fifo_write_allowed(MIF_CMD,0,0x34));
    assert(!marble_i2c_fifo_write_allowed(MIF_CMD,(1u<<27)|(0x36<<9)|4u,0x34));
    assert(!marble_i2c_fifo_write_allowed(MIF_CLEAR,~0u,0x34));assert(!marble_i2c_fifo_read_allowed(0x880254));
    assert(!marble_i2c_fifo_write_allowed(MIF_S_CLEAR,0x10,0x34));assert(marble_i2c_fifo_write_allowed(MIF_S_CLEAR,0xc000000,0x34));
    puts("PASS ID-only FIFO: both fixed addresses, two bounded repeated-start commands, MSB chip ID, every callback fault, NACK, timeout/abort and dirty abort; disallows amplifier data writes and foreign addresses");
}
