#include <stdint.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "../../xnuloader/marble_glink_data.h"
static void word(uint8_t *p,uint32_t n){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(n>>(8*i));}
static void frame(uint8_t *f,unsigned cmd,unsigned n,unsigned left)
{memset(f,0,528);word(f,cmd|(3u<<16));word(f+4,1);word(f+8,n);word(f+12,left);for(unsigned i=0;i<n && i<512;i++)f[16+i]=(uint8_t)i;}
int main(void)
{
    uint8_t f[528];MarbleGlinkAssembly s={0},saved;MarbleGlinkControl m;
    for(unsigned split=1;split<512;split++) {
        memset(&s,0,sizeof(s));frame(f,9,split,512-split);
        assert(marble_glink_frame(f,(16+split+7)&~7u,&m,2)==1);
        assert(marble_glink_fragment(&s,9,f,m.bytes,3));assert(s.active && s.used==split);
        saved=s;assert(!marble_glink_fragment(&s,9,f,m.bytes,3));assert(!memcmp(&s,&saved,sizeof(s)));
        frame(f,12,512-split,0);assert(marble_glink_fragment(&s,12,f,(16+512-split+7)&~7u,3));
        assert(!s.active && s.used==512);for(unsigned i=0;i<split;i++)assert(s.data[i]==(uint8_t)i);
    }
    for(unsigned n=0;n<=513;n++) {
        frame(f,9,n,0);memset(&s,0,sizeof(s));unsigned bytes=(16+n+7)&~7u;
        assert(marble_glink_fragment(&s,9,f,bytes,3)==(n>0 && n<=512));
        if(!n || n>512)continue;
        assert(marble_glink_frame(f,bytes,&m,0)==-1 && marble_glink_frame(f,bytes,&m,1)==-1);
        for(unsigned len=0;len<bytes;len++) {memset(&s,0,sizeof(s));assert(!marble_glink_fragment(&s,9,f,len,3));}
        memset(&s,0,sizeof(s));assert(!marble_glink_fragment(&s,9,f,bytes,4));
    }
    memset(&s,0,sizeof(s));frame(f,9,20,12);assert(marble_glink_fragment(&s,9,f,40,3));saved=s;
    frame(f,12,8,5);assert(!marble_glink_fragment(&s,12,f,24,3));assert(!memcmp(&s,&saved,sizeof(s)));
    frame(f,12,13,0);assert(!marble_glink_fragment(&s,12,f,32,3));
    frame(f,12,12,0);word(f+4,2);assert(!marble_glink_fragment(&s,12,f,32,3));
    word(f+4,1);assert(marble_glink_fragment(&s,12,f,32,3));
    frame(f,14,0,0);word(f+4,0);assert(marble_glink_frame(f,8,&m,2)==1);
    assert(marble_glink_frame(f,8,&m,1)==-1);
    puts("PASS GLINK DATA: all 512-byte splits, zero/overflow/truncation, exact remaining length, channel/intent binding, unchanged rejected state, isolated mode");
}
