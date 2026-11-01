/* Explicit RAM-support-disk tail, outside APFS and input ring. Separate cache
 * lines for immutable capability, Darwin request, and EFI acknowledgement. */
#ifndef MARBLE_BRIGHTNESS_MAILBOX_H
#define MARBLE_BRIGHTNESS_MAILBOX_H
#include <stdint.h>
#include <stddef.h>
#define MB_OFFSET 0x802000u
#define MB_MAGIC UINT64_C(0x314c424d52414d)
typedef struct {
    uint64_t magic;
    uint32_t version,ready,original,minimum,maximum,reserved;
    uint64_t pad[4];
    uint64_t sequence;
    uint32_t milli,request_reserved;
    uint64_t request_pad[6];
    uint64_t acknowledged;
    uint32_t result,actual,accepted_milli,failed;
    uint64_t response_pad[5];
} MarbleBrightnessMailbox;
_Static_assert(offsetof(MarbleBrightnessMailbox,sequence)==64,"brightness request line");
_Static_assert(offsetof(MarbleBrightnessMailbox,acknowledged)==128,"brightness response line");
_Static_assert(sizeof(MarbleBrightnessMailbox)==192,"brightness RAM ABI");
static int marble_brightness_request_valid(uint64_t seq,uint64_t after,uint64_t ack,unsigned milli,unsigned reserved)
{return seq && !(seq&1) && seq==after && seq>ack && milli<=1000 && !reserved;}
static unsigned marble_brightness_target(unsigned original,unsigned minimum,unsigned milli)
{
    if(original>4095 || minimum<8 || minimum>=original || milli>1000 ||
       ((original>327)!=(minimum>327)))return 0;
    return minimum+(original-minimum)*milli/1000;
}
#endif
