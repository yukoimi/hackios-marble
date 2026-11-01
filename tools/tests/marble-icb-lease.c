#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../xnuloader/marble_icb_lease.h"
static uint64_t ram[4096];
static int readword(uint64_t at,uint64_t *out){if(!at || (at&3) || at>sizeof(ram)-8)return 0;memcpy(out,(unsigned char *)ram+at,8);return 1;}
static void put(uint64_t at,uint64_t v){memcpy((unsigned char *)ram+at,&v,8);}
int main(void) {
    MarbleNpaClientProtocol protocol={0};uint64_t create=0,destroy=0,base=0x80000000;
    protocol.revision=0x10003;protocol.create_sync_client_ex=base+0x1448;
    protocol.other[11]=base+0x15b8;protocol.destroy_client=base+0x15e4;
    assert(marble_npa_client_binding(&protocol,base,&create,&destroy));assert(destroy==base+0x15e4);
    protocol.destroy_client=protocol.other[11];assert(!marble_npa_client_binding(&protocol,base,&create,&destroy));
    MarbleIcbRequestList before={0},after={0};
    put(0x100+0xc8,1ULL|(10ULL<<32));put(0x100+0xd0,0x400);put(0x400,0x600);put(0x610,0x900);
    assert(marble_icb_requests(readword,0x100,1,&before));
    put(0x100+0xc8,2ULL|(10ULL<<32));put(0x408,0x700);put(0x710,0xa00);
    assert(marble_icb_requests(readword,0x100,1,&after));assert(marble_icb_requests_same(&before,&after,1,0x700));
    assert(!marble_icb_requests_same(&before,&after,1,0x701));
    put(0x100+0xc8,1ULL|(20ULL<<32));
    assert(marble_icb_requests(readword,0x100,1,&after));assert(marble_icb_requests_same(&before,&after,0,0));
    put(0x600,1);assert(marble_icb_requests(readword,0x100,1,&after));assert(!marble_icb_requests_same(&before,&after,0,0));put(0x600,0);
    put(0x100,1);assert(marble_icb_requests(readword,0x100,1,&after));assert(!marble_icb_requests_same(&before,&after,0,0));put(0x100,0);
    put(0x100+0xc8,65ULL|(70ULL<<32));assert(!marble_icb_requests(readword,0x100,1,&after));
    put(0x100+0xc8,2ULL|(10ULL<<32));put(0x408,0x600);assert(!marble_icb_requests(readword,0x100,1,&after));
    put(0x100+0xc8,11ULL|(10ULL<<32));assert(!marble_icb_requests(readword,0x100,1,&after));
    assert(!marble_icb_requests(readword,UINT64_MAX-3,0,&after));
    puts("PASS ICB native lists: retained capacity, exact member/request closure, state changes, duplicates, limits and overflow");
}
