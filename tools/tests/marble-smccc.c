#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "../../xnuloader/marble_smccc.h"
int main(void)
{
    uint64_t request[8]={0x42000601,1,0x02000206,0,0,0,0,0},original[8],result[8]={0};
    memcpy(original,request,sizeof(request));
    assert(marble_smccc_next(request,result,1,5)==MARBLE_SMC_DONE);
    for(unsigned raw=2;raw<=3;raw++) {
        result[0]=raw;result[6]=0xabcd;
        assert(marble_smccc_next(request,result,1,5)==(int)raw);
        assert(!memcmp(request,original,sizeof(request)));
    }
    result[0]=1;result[6]=0x12345678;
    for(unsigned calls=1;calls<=65;calls++)for(unsigned time=249999;time<=250001;time++) {
        memcpy(request,original,sizeof(request));
        int resume=calls<64 && time<250000;
        assert(marble_smccc_next(request,result,calls,time)==(resume?MARBLE_SMC_RESUME:MARBLE_SMC_LIMIT));
        if(resume) {
            assert(request[0]==1 && request[6]==result[6]);
            for(unsigned i=1;i<8;i++)if(i!=6)assert(request[i]==original[i]);
        } else assert(!memcmp(request,original,sizeof(request)));
    }
    for(unsigned i=0;i<64;i++) {
        result[0]=(1ULL<<i)+4;memcpy(request,original,sizeof(request));
        assert(marble_smccc_next(request,result,1,1)==MARBLE_SMC_ERROR);
        assert(!memcmp(request,original,sizeof(request)));
    }
    result[0]=~0ULL;assert(marble_smccc_next(request,result,1,1)==MARBLE_SMC_ERROR);
    assert(marble_smccc_next(NULL,result,1,1)==MARBLE_SMC_ERROR);
    assert(marble_smccc_next(request,NULL,1,1)==MARBLE_SMC_ERROR);
    assert(marble_smccc_next(request,result,0,1)==MARBLE_SMC_ERROR);
    result[0]=1;
    for(unsigned calls=1;calls<=129;calls++)for(unsigned time=999999;time<=1000001;time++) {
        memcpy(request,original,sizeof(request));int resume=calls<128 && time<1000000;
        assert(marble_smccc_next_limited(request,result,calls,time,128,1000000)==(resume?MARBLE_SMC_RESUME:MARBLE_SMC_LIMIT));
        if(resume)for(unsigned i=1;i<8;i++)if(i!=6)assert(request[i]==original[i]);
    }
    assert(marble_smccc_next_limited(request,result,1,1,129,1000000)==MARBLE_SMC_ERROR);
    assert(marble_smccc_next_limited(request,result,1,1,128,1000001)==MARBLE_SMC_ERROR);
    for(unsigned raw=2;raw<=3;raw++) {
        result[0]=raw;memcpy(request,original,sizeof(request));
        assert(marble_smccc_next_limited(request,result,1,1,128,1000000)==(int)raw);
        assert(!memcmp(request,original,sizeof(request)));
    }
    puts("PASS bounded SMCCC: WAITQ tokens never replayed, only INTERRUPTED resumes with A6 cookie, time/call limits and error preservation");
}
