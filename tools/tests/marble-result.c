#include "../../xnuloader/loader_result.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void)
{
    char result[64];
    loader_format_result(result,sizeof(result),"running",0,0);assert(!strcmp(result,"running 0 0"));
    loader_format_result(result,sizeof(result),"returned",UINT64_MAX,UINT64_MAX);
    assert(!strcmp(result,"returned 18446744073709551615 ffffffffffffffff"));
    loader_format_result(result,sizeof(result),"hung",1343535,0);assert(!strcmp(result,"hung 1343535 0"));
    /* No log buffer is passed: saturation can no longer affect this result. */
    char guarded[6]={'L','a','b','c','d','R'};
    loader_format_result(guarded+1,4,"returned",1,2);
    assert(guarded[0]=='L' && guarded[5]=='R' && !strcmp(guarded+1,"ret"));
    char zero='Q';loader_format_result(&zero,0,"running",1,2);assert(zero=='Q');
    loader_format_result(&zero,1,"running",1,2);assert(zero==0);
    puts("loader result: PASS independent status, uint64 limits, bounded/truncated output, zero capacity");return 0;
}
