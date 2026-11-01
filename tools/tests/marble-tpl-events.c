#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../xnuloader/marble_tpl_events.h"
static uint64_t memory[0x40000/8];
static unsigned reads,change_pending;
static const uint64_t base=0x80000000;
static int read_word(void *cookie,uint64_t at,uint64_t *value)
{
    (void)cookie;reads++;
    if(at<base || at>=base+sizeof(memory) || (at&7))return 0;
    *value=memory[(at-base)/8];
    if(change_pending && at==base+0x1a318 && reads>1)*value=0;
    return 1;
}
static void put(uint64_t at,uint64_t value){assert(at>=base && at<base+sizeof(memory) && !(at&7));memory[(at-base)/8]=value;}
static uint64_t setup(unsigned count)
{
    memset(memory,0,sizeof(memory));reads=change_pending=0;
    uint64_t head=base+0x1a620+8*16;
    put(base+0x16630,8);put(base+0x1a318,count?1<<8:0);
    put(head,count?base+0x30048:head);put(head+8,count?base+0x30048+(count-1)*0x100:head);
    for(unsigned i=0;i<count;i++) {
        uint64_t event=base+0x30000+i*0x100,link=event+0x48;
        put(event,0x746e7665);put(event+8,0x80000200);put(event+0x20,8);put(event+0x28,base+0x1000);put(event+0x30,base+0x2000);
        put(link,i+1==count?head:link+0x100);put(link+8,i?link-0x100:head);
    }
    return head;
}
int main(void)
{
    MarbleTplView view;setup(0);assert(marble_tpl_events_read(read_word,0,base,&view) && view.valid && !view.count);
    uint64_t head=setup(2);assert(marble_tpl_events_read(read_word,0,base,&view) && view.count==2 && view.event[0].callback==base+0x1000);
    setup(2);put(base+0x30150,head);assert(!marble_tpl_events_read(read_word,0,base,&view));
    setup(1);put(base+0x30000,0);assert(!marble_tpl_events_read(read_word,0,base,&view));
    setup(1);put(base+0x30028,0xf100000);assert(!marble_tpl_events_read(read_word,0,base,&view));
    setup(1);change_pending=1;assert(!marble_tpl_events_read(read_word,0,base,&view));
    setup(33);assert(!marble_tpl_events_read(read_word,0,base,&view) && view.count==32 && view.truncated && reads<1000);
    setup(32);assert(marble_tpl_events_read(read_word,0,base,&view) && view.count==32);
    printf("PASS passive TPL event codec: exact layout, empty/two/32 nodes, reject broken links/signature/unmapped callback/concurrent change, bounded truncation; no callback execution or writes\n");
}
