#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../xnuloader/marble_aw2032_quiet.h"
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
    Model *m=ctx;assert(marble_aw2032_quiet_write_allowed(a,v,m->address,m->value));
    if(a==MIF_CMD){m->stage=v>>27;m->commands++;m->irq=m->stage==2?0x8000001u:0;if(m->stage==2){m->s_irq=0xc000000;m->receiver_started=1;}}
    else if(a==MIF_TX){assert(m->stage==1 && !m->sent && v==marble_aw2032_quiet_word(m->value));m->sent=1;m->irq=1;}
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
    for(unsigned address=0x34;address<=0x35;address++)for(unsigned low=0;low<64;low++)if(low&0x10) {
        uint16_t before=(uint16_t)(0x1240|low);
        Model m={.address=address,.value=before,.rx_watermark=13};
        MarbleI2cFifoOps o={&m,read_word,write_word,now,delay};MarbleI2cFifoResult p={0};
        assert(marble_aw2032_quiet_write(&o,address,0x4003,before,&p));
        assert(p.done && p.commands==1 && p.tx_words==1 && !p.rx_words && !p.dirty && !p.aborts);
        assert(!marble_aw2032_quiet_write(&o,address,0x4003,before,&p));unsigned calls=m.calls;
        assert((marble_aw2032_quiet_value(before)&0x3f)==low);
        assert((marble_aw2032_quiet_value(before)>>6)==960);
        uint32_t word=marble_aw2032_quiet_word(before);
        assert((word&255)==5 && (word>>8&255)==0xf0 && (word>>16)==low);
        for(unsigned fail=1;fail<=calls;fail++) {
            m=(Model){.address=address,.value=before,.rx_watermark=13,.fail_at=fail};p=(MarbleI2cFifoResult){0};
            assert(!marble_aw2032_quiet_write(&o,address,0x4003,before,&p));assert(p.commands<=1 && m.commands<=1 && p.tx_words<=1 && !p.rx_words && m.us<=30000);
        }
        for(unsigned mode=0;mode<3;mode++) {
            m=(Model){.address=address,.value=before,.rx_watermark=13,.stalled=mode!=1,.nack=mode==1,.abort_stuck=mode==2};p=(MarbleI2cFifoResult){0};
            assert(!marble_aw2032_quiet_write(&o,address,0x4003,before,&p));assert(p.aborts==1 && p.dirty==(mode==2) && m.us<=30000);
        }
    }
    Model m={0};MarbleI2cFifoOps o={&m,read_word,write_word,now,delay};MarbleI2cFifoResult p={0};
    for(unsigned ctrl=0;ctrl<4;ctrl++)for(unsigned mute=0;mute<2;mute++)if(ctrl!=3 || !mute) {
        assert(!marble_aw2032_quiet_write(&o,0x34,ctrl,mute?0x38:0x28,&p));assert(!m.calls && !p.attempted);
    }
    assert(!marble_aw2032_quiet_write(&o,0x36,3,0x38,&p));assert(!m.calls);
    assert(!marble_aw2032_quiet_write_allowed(MIF_TX,0x38f004,0x34,0x38));
    assert(!marble_aw2032_quiet_write_allowed(MIF_TX,0x380005,0x34,0x38));
    assert(!marble_aw2032_quiet_write_allowed(MIF_TX_LEN,1,0x34,0x38));
    assert(!marble_aw2032_quiet_write_allowed(MIF_CMD,(2u<<27)|(0x34<<9),0x34,0x38));
    assert(!marble_aw2032_quiet_write_allowed(MIF_CMD,(1u<<27)|(0x34<<9)|4,0x34,0x38));
    assert(!marble_aw2032_quiet_write_allowed(MIF_RX_LEN,2,0x34,0x38));
    puts("PASS AW2032 quiet write: fixed SYSCTRL2, 90dB/HMUTE preserving controls, byte order, both devices, all callback errors, NACK, bounded abort; no power-up/OTP/calibration/boost");
}
