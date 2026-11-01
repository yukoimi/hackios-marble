#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "../../xnuloader/marble_rpmh.h"
static uint64_t ram[32768];
static int readword(uint64_t at,uint64_t *out) {
    if(!at || (at&3) || at>sizeof(ram)-8)return 0;
    memcpy(out,(unsigned char *)ram+at,8);return 1;
}
static void put(uint64_t at,uint64_t value) {memcpy((unsigned char *)ram+at,&value,8);}
static void fixture(void) {
    memset(ram,0,sizeof(ram));
    put(0x100,1ULL|(2ULL<<32));put(0x108,0x200);put(0x110,0x700);put(0x118,0x300);
    put(0x300,0x400);put(0x308,0x500);put(0x400,0x50);put(0x500,0x50);
    put(0x200,0x300b0);put(0x208,0x600);put(0x218,0x300c0);put(0x600,2);
}
int main(void) {
    static MarbleRpmhSnapshot s;
    MarbleRpmhQueue q;
    fixture();assert(marble_rpmh_snapshot(readword,0x100,&s));assert(s.resource_count==2 && s.vote_count==1);
    put(0x640,0x600);assert(!marble_rpmh_snapshot(readword,0x100,&s));
    fixture();put(0x218,0x300b0);assert(!marble_rpmh_snapshot(readword,0x100,&s));
    fixture();put(0x100,1ULL|(513ULL<<32));assert(!marble_rpmh_snapshot(readword,0x100,&s));
    fixture();put(0x400,0x50ULL|(1ULL<<32));put(0x408,0x800);put(0x410,0x800);
    assert(marble_rpmh_queue(readword,0x400,0x50,&q));
    put(0x850,0x800);assert(!marble_rpmh_queue(readword,0x400,0x50,&q));
    put(0x850,0);put(0x410,0x900);assert(!marble_rpmh_queue(readword,0x400,0x50,&q));
    put(0x410,0x800);put(0x400,0x50ULL|(65ULL<<32));assert(!marble_rpmh_queue(readword,0x400,0x50,&q));
    fixture();put(0x408,0x800);assert(!marble_rpmh_queue(readword,0x400,0x50,&q));
    fixture();put(0x800,2);put(0x808,0x1234);
    for(unsigned i=0;i<4;i++){put(0x810+8*i,0x900+0x20*i);put(0x900+0x20*i,i<2?0x10:0x18);}
    assert(marble_rpmh_client_empty(readword,0x800,0x1234));
    for(unsigned off=0x30;off<=0x48;off+=8){put(0x800+off,1);assert(!marble_rpmh_client_empty(readword,0x800,0x1234));put(0x800+off,0);}
    put(0x908,0xdead);assert(!marble_rpmh_client_empty(readword,0x800,0x1234));
    fixture();
    /* Native DAL heap allocations on marble are four-byte aligned. */
    put(0x118,0x304);put(0x304,0x404);put(0x30c,0x504);
    put(0x404,0x50);put(0x40c,0);put(0x414,0);put(0x504,0x50);put(0x50c,0);put(0x514,0);
    assert(marble_rpmh_snapshot(readword,0x100,&s));
    assert(!marble_rpmh_words(readword,UINT64_MAX-7,ram,2));
    assert(!marble_rpmh_words(readword,1,ram,1));
    puts("PASS native RPMh bounded metadata: list cycles/count/tail, overlapping votes, sorted resources, request/callback/explicit client refusal, pointer overflow");
}
