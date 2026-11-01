#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../xnuloader/marble_icb.h"
static uint64_t ram[4096];
static int readword(uint64_t at,uint64_t *out) {
    if(!at || (at&3) || at>sizeof(ram)-8)return 0;
    memcpy(out,(unsigned char *)ram+at,8);return 1;
}
static void put(uint64_t at,uint64_t v){memcpy((unsigned char *)ram+at,&v,8);}
static void fixture(void) {
    memset(ram,0,sizeof(ram));put(0x108,1);put(0x110,0x200);put(0x200,0x300);
    put(0x310,0x400);put(0x400,1ULL<<32);put(0x408,0x500);
    put(0x500,1);put(0x508,0x600);put(0x600,0x700);put(0x608,0x800);
}
int main(void) {
    static MarbleIcbSnapshot s;
    fixture();assert(marble_icb_snapshot(readword,0x100,&s));assert(s.trees==1 && s.count==1);
    put(0x400,0);assert(marble_icb_snapshot(readword,0x100,&s)); /* valid master0 / slave0 */
    put(0x410,0x400);assert(!marble_icb_snapshot(readword,0x100,&s));
    fixture();put(0x500,17);assert(!marble_icb_snapshot(readword,0x100,&s));
    fixture();put(0x500,0);assert(!marble_icb_snapshot(readword,0x100,&s));
    fixture();put(0x600,0);assert(!marble_icb_snapshot(readword,0x100,&s));
    fixture();put(0x108,9);assert(!marble_icb_snapshot(readword,0x100,&s));
    fixture();put(0x410,0x900);put(0x900,2ULL<<32);put(0x908,0x500);
    assert(!marble_icb_snapshot(readword,0x100,&s)); /* invalid left ordering */
    put(0x900,0);assert(marble_icb_snapshot(readword,0x100,&s));assert(s.count==2);
    put(0x910,0x400);assert(!marble_icb_snapshot(readword,0x100,&s));
    fixture();put(0x310,UINT64_MAX-3);assert(!marble_icb_snapshot(readword,0x100,&s));
    puts("PASS native ICB metadata: master0/slave0, strict tree ordering, cycles, hop/tree limits, null pointers and overflow");
}
