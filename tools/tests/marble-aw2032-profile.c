#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../xnuloader/marble_aw2032_profile.h"
typedef struct {MarbleAw2032Pair pair;unsigned readback;unsigned reg;uint16_t value;unsigned address,calls,fail_at,stage,sent,consumed,stalled,nack,bad_count,abort_stuck,commands,aborts,watermark,rx_watermark,receiver_started,enable_summary;uint32_t irq,s_irq;uint64_t us;} Model;
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
    Model *m=ctx;assert(marble_aw2032_profile_write_allowed(a,v,m->address,m->pair,m->readback));
    if(a==MIF_CMD){m->stage=v>>27;m->commands++;m->irq=m->stage==2?0x8000001u:0;if(m->stage==2){m->s_irq=0xc000000;m->receiver_started=1;}}
    else if(a==MIF_TX){assert(m->stage==1 && !m->sent && v==(m->readback?m->pair.reg:marble_aw2032_profile_word(m->pair)));m->sent=1;m->irq=1;}
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
    for(unsigned address=0x34;address<=0x35;address++)for(unsigned i=0;i<MARBLE_AW2032_PROFILE_WORDS;i++) {
        MarbleAw2032Pair pair={0};assert(marble_aw2032_profile_pair(address,i,0x4003,0xf038,0xc3ff,0x4015,&pair));
        if(pair.reg==4)assert((pair.value&3)==3);
        if(pair.reg==5)assert((pair.value&0xffd0)==0xf010);
        assert(pair.reg<0x70 || pair.reg==0x56);
        for(unsigned readback=0;readback<2;readback++) {
            Model m={.address=address,.pair=pair,.value=pair.value,.rx_watermark=13,.readback=readback};
            MarbleI2cFifoOps o={&m,read_word,write_word,now,delay};MarbleI2cFifoResult p={0};
            int ok=readback?marble_aw2032_read_register_raw(&o,address,pair.reg,&p):marble_aw2032_profile_write(&o,address,pair,&p);
            assert(ok && p.done && !p.dirty && !p.aborts && p.commands==(readback?2u:1u));
            assert(p.tx_words==1 && p.rx_words==readback);
            if(readback)assert(p.chip_id==pair.value);
            unsigned calls=m.calls;
            for(unsigned fail=1;fail<=calls;fail++) {
                m=(Model){.address=address,.pair=pair,.value=pair.value,.rx_watermark=13,.readback=readback,.fail_at=fail};p=(MarbleI2cFifoResult){0};
                ok=readback?marble_aw2032_read_register_raw(&o,address,pair.reg,&p):marble_aw2032_profile_write(&o,address,pair,&p);
                assert(!ok && m.us<=30000 && p.commands<=(readback?2u:1u));
            }
            for(unsigned mode=0;mode<3;mode++) {
                m=(Model){.address=address,.pair=pair,.value=pair.value,.rx_watermark=13,.readback=readback,.stalled=mode!=1,.nack=mode==1,.abort_stuck=mode==2};p=(MarbleI2cFifoResult){0};
                ok=readback?marble_aw2032_read_register_raw(&o,address,pair.reg,&p):marble_aw2032_profile_write(&o,address,pair,&p);
                assert(!ok && p.aborts==1 && p.dirty==(mode==2) && m.us<=30000);
            }
        }
        MarbleAw2032Pair bad=pair;bad.value^=4;
        if(pair.reg!=0x56)assert(!marble_aw2032_profile_write_allowed(MIF_TX,marble_aw2032_profile_word(bad),address,bad,0));
    }
    for(unsigned voltage=0;voltage<1024;voltage++)for(unsigned current=0;current<1024;current++) {
        uint16_t v=0;assert(marble_aw2032_vcalb((uint16_t)voltage,(uint16_t)current,&v));
        int a=(int)voltage-(voltage&512?1024:0),b=(int)current-(current&512?1024:0);
        assert(v==8192*(1000+b)/(1000+a));assert(v>=2645 && v<=25365);
    }
    MarbleAw2032Pair pair={0};
    assert(!marble_aw2032_profile_pair(0x36,0,3,16,0,0,&pair));
    assert(!marble_aw2032_profile_pair(0x34,45,3,16,0,0,&pair));
    assert(!marble_aw2032_profile_pair(0x34,0,0,16,0,0,&pair));
    assert(!marble_aw2032_profile_pair(0x34,0,3,0,0,0,&pair));
    for(unsigned reg=0x70;reg<0x7e;reg++) {
        pair=(MarbleAw2032Pair){(uint16_t)reg,0};assert(!marble_aw2032_profile_write_allowed(MIF_TX,0,0x34,pair,0));
    }
    pair=(MarbleAw2032Pair){4,0xb440};assert(!marble_aw2032_profile_write_allowed(MIF_TX,marble_aw2032_profile_word(pair),0x34,pair,0));
    pair=(MarbleAw2032Pair){5,0x5a9};assert(!marble_aw2032_profile_write_allowed(MIF_TX,marble_aw2032_profile_word(pair),0x34,pair,0));
    puts("PASS board AW2032 profile: 45 fixed entries per chip, independent readback, every callback fault, NACK/timeout/dirty abort, all 1M OTP combinations, power/mute/table invariants and OTP write rejection");
}
