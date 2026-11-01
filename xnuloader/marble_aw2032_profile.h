/* Board Music profile, never a caller-supplied register stream. Runtime
 * preserves PWDN/AMPPD, holds HMUTE/90dB and never enables the speaker switch.
 * Source/ACF/board binding is independently checked before each image build. */
#ifndef MARBLE_AW2032_PROFILE_H
#define MARBLE_AW2032_PROFILE_H
#include "marble_aw2032_music.h"
#include "marble_aw2032_quiet.h"
#include "marble_aw2032_write.h"
#define MARBLE_AW2032_PROFILE_WORDS (MARBLE_AW2032_MUSIC_WORDS+1u)
static int marble_aw2032_signed10(uint16_t word)
{int v=word&1023;return v&512?v-1024:v;}
static int marble_aw2032_vcalb(uint16_t efrh,uint16_t efrm1,uint16_t *out)
{
    int voltage=1000+marble_aw2032_signed10(efrh),current=1000+marble_aw2032_signed10(efrm1);
    if(!out || voltage<=0 || current<=0)return 0;
    int v=8192*current/voltage;if(v<=0 || v>65535)return 0;*out=(uint16_t)v;return 1;
}
static int marble_aw2032_profile_pair(unsigned address,unsigned index,uint16_t sysctrl,uint16_t sysctrl2,uint16_t efrh,uint16_t efrm1,MarbleAw2032Pair *pair)
{
    if(!pair || (address!=0x34 && address!=0x35) || index>=MARBLE_AW2032_PROFILE_WORDS || (sysctrl&3)!=3 || !(sysctrl2&16))return 0;
    if(index==MARBLE_AW2032_MUSIC_WORDS) {
        pair->reg=0x56;return marble_aw2032_vcalb(efrh,efrm1,&pair->value);
    }
    *pair=marble_aw2032_music[address-0x34][index];
    if(pair->reg==3)pair->value=65535;
    if(pair->reg==4)pair->value=(pair->value&~3u)|(sysctrl&3);
    if(pair->reg==5)pair->value=marble_aw2032_quiet_value(pair->value);
    return 1;
}
static uint32_t marble_aw2032_profile_word(MarbleAw2032Pair pair)
{return pair.reg|((uint32_t)(pair.value>>8)<<8)|((uint32_t)(pair.value&255)<<16);}
static int marble_aw2032_profile_write_allowed(uint64_t a,uint32_t v,unsigned address,MarbleAw2032Pair pair,int readback)
{
    if(address!=0x34 && address!=0x35)return 0;
    /* Calibration is the only chip-specific value. Even a wrong caller
     * cannot clear power-down/mute or substitute boost/test table values. */
    int allowed=pair.reg==0x56 && pair.value>=2645 && pair.value<=25365;
    for(unsigned i=0;i<MARBLE_AW2032_MUSIC_WORDS;i++) {
        MarbleAw2032Pair expected;
        if(!marble_aw2032_profile_pair(address,i,3,16,0,0,&expected))return 0;
        allowed|=pair.reg==expected.reg && pair.value==expected.value;
    }
    if(!allowed)return 0;
    if(a==MIF_TX_LEN)return v==(readback?1u:3u);
    if(a==MIF_RX_LEN)return readback && v==2;
    if(a==MIF_TX)return v==(readback?pair.reg:marble_aw2032_profile_word(pair));
    if(a==MIF_CMD)return readback?(v==((1u<<27)|(address<<9)|4) || v==((2u<<27)|(address<<9))):v==((1u<<27)|(address<<9));
    return marble_i2c_fifo_write_allowed(a,v,address);
}
static int marble_aw2032_profile_write(const MarbleI2cFifoOps *o,unsigned address,MarbleAw2032Pair pair,MarbleI2cFifoResult *p)
{
    return marble_aw2032_profile_write_allowed(MIF_TX,marble_aw2032_profile_word(pair),address,pair,0) &&
        marble_aw2032_write_raw(o,address,pair.reg,pair.value,p);
}
#endif
