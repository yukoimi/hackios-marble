/* SMCCC register transition rules from the pinned Xiaomi SCM implementation.
 * WAITQ 2/3 are observable outcomes, never INTERRUPTED resumes. No I/O here. */
#ifndef MARBLE_SMCCC_H
#define MARBLE_SMCCC_H
#include <stdint.h>
enum { MARBLE_SMC_ERROR=-1, MARBLE_SMC_DONE=0, MARBLE_SMC_RESUME=1,
       MARBLE_SMC_WAIT_SLEEP=2, MARBLE_SMC_WAIT_WAKE=3, MARBLE_SMC_LIMIT=4 };
static int marble_smccc_next_limited(uint64_t request[8],const uint64_t result[8],
    unsigned calls,uint64_t elapsed_us,unsigned max_calls,uint64_t max_us)
{
    if(!request || !result || !calls || !((max_calls==64 && max_us==250000) ||
       (max_calls==128 && max_us==1000000)))return MARBLE_SMC_ERROR;
    if(result[0]==0)return MARBLE_SMC_DONE;
    if(result[0]==2)return MARBLE_SMC_WAIT_SLEEP;
    if(result[0]==3)return MARBLE_SMC_WAIT_WAKE;
    if(result[0]!=1)return MARBLE_SMC_ERROR;
    if(calls>=max_calls || elapsed_us>=max_us)return MARBLE_SMC_LIMIT;
    /* Retain original x1..x5/x7; only a0 and the QCOM A6 cookie change. */
    request[0]=1;request[6]=result[6];
    return MARBLE_SMC_RESUME;
}
static int marble_smccc_next(uint64_t request[8],const uint64_t result[8],
    unsigned calls,uint64_t elapsed_us)
{return marble_smccc_next_limited(request,result,calls,elapsed_us,64,250000);}
#endif
