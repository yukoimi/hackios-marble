#ifndef MARBLE_XO_NPA_H
#define MARBLE_XO_NPA_H
#include <stdint.h>
#include <stddef.h>
/* Scalar max aggregation follows Qualcomm npa_plugins.c semantics, with
 * bounded full rescans for the few clients of this dedicated XO provider. */
typedef struct {uint32_t type,state;} MarbleXoRequest;
static int marble_xo_aggregate(const MarbleXoRequest *requests,unsigned count,uint32_t maximum,uint32_t *required,uint32_t *suppressible)
{
    if(count>16 || !maximum)return 0;
    uint32_t r=0,s=0;
    for(unsigned i=0;i<count;i++) {
        uint32_t value=requests[i].state==UINT32_MAX?maximum:requests[i].state;
        if(value>maximum)return 0;
        if(requests[i].type==0x40) {if(value>r)r=value;}
        else if(requests[i].type==0x800) {if(value>s)s=value;}
        else return 0;
    }
    *required=r;*suppressible=s;return 1;
}
typedef struct {
    uint64_t revision,init,create,complete,define_node,scalar;
} MarbleNpaProviderProtocol;
_Static_assert(offsetof(MarbleNpaProviderProtocol,define_node)==0x20,"native NPA DefineNodeCb offset");
_Static_assert(offsetof(MarbleNpaProviderProtocol,scalar)==0x28,"native NPA ScalarRequest offset");
#endif
