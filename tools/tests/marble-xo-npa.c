#include <assert.h>
#include <stdio.h>
#include "../../xnuloader/marble_xo_npa.h"
int main(void) {
    MarbleXoRequest requests[16];uint32_t required=0,suppressible=0;
    for(unsigned types=0;types<256;types++)for(unsigned values=0;values<256;values++) {
        unsigned expected_r=0,expected_s=0;
        for(unsigned i=0;i<8;i++) {
            unsigned on=(values>>i)&1,supp=(types>>i)&1;
            requests[i]=(MarbleXoRequest){supp?0x800:0x40,on?UINT32_MAX:0};
            if(supp)expected_s|=on;else expected_r|=on;
        }
        assert(marble_xo_aggregate(requests,8,1,&required,&suppressible));
        assert(required==expected_r && suppressible==expected_s);
    }
    requests[0]=(MarbleXoRequest){0x40,1};requests[1]=(MarbleXoRequest){0x40,1};
    assert(marble_xo_aggregate(requests,2,1,&required,&suppressible) && required==1);
    requests[0].state=0;assert(marble_xo_aggregate(requests,2,1,&required,&suppressible) && required==1);
    requests[1].state=0;assert(marble_xo_aggregate(requests,2,1,&required,&suppressible) && required==0);
    requests[0].type=0x400;assert(!marble_xo_aggregate(requests,1,1,&required,&suppressible));
    requests[0]=(MarbleXoRequest){0x40,2};assert(!marble_xo_aggregate(requests,1,1,&required,&suppressible));
    assert(!marble_xo_aggregate(requests,17,1,&required,&suppressible));
    puts("PASS XO aggregation: 65536 type/state combinations, max remapping, overlapping leases, final release and invalid clients");
}
