#ifndef MARBLE_TPL_EVENTS_FW_H
#define MARBLE_TPL_EVENTS_FW_H
#include "marble_tpl_events.h"
static struct {uint64_t base,elapsed[3];unsigned taken[3];MarbleTplView view[3];} marble_tpl_observer;
static int marble_tpl_read(void *cookie,uint64_t at,uint64_t *value)
{(void)cookie;return marble_npa_ram_word(at,value);}
static int marble_tpl_events_qualify(void)
{
    static const uint8_t digest[32]={0x11,0x3d,0xe7,0x03,0xaf,0x0d,0x22,0x1c,0x71,0x29,0x4b,0x58,0xa6,0xb8,0x57,0x7e,0x28,0x97,0x0c,0xb7,0x7d,0xf3,0xf6,0x53,0xc6,0x18,0x66,0x0d,0xd2,0x90,0xef,0x75};
    uint64_t restore=(uint64_t)(uintptr_t)BS->restore_tpl;
    if(restore<0x80003a40 || restore>=0x500000000ULL)return 0;
    uint64_t base=restore-0x3a40;
    unsigned code=!(base&4095) && marble_code_digest(marble_npa_ram_word,base+0x3a40,0x128,digest);
    unsigned raise=(uint64_t)(uintptr_t)BS->raise_tpl==base+0x39ec;
    unsigned ok=code && raise;
    print("aud-tpl-bindings: raise=%x restore=%x allocator=%x raise_match=%d allocator_match=%d code_match=%d\n",
        (uint64_t)(uintptr_t)BS->raise_tpl,restore,(uint64_t)(uintptr_t)BS->allocate_pages,(uint64_t)raise,
        (uint64_t)((uint64_t)(uintptr_t)BS->allocate_pages==base+0x45e4),(uint64_t)code);
    uint64_t words[8]={0};unsigned readable=1;
    for(unsigned i=0;i<8;i++)if(!marble_npa_ram_word(restore+8*i,&words[i]))readable=0;
    if(readable)print("aud-tpl-restore-code: callback=%x code=%x,%x,%x,%x,%x,%x,%x,%x\n",restore,
        words[0],words[1],words[2],words[3],words[4],words[5],words[6],words[7]);
    if(ok)marble_tpl_observer.base=base;
#if defined(XNU_MARBLE_AUDIO_TPL_SAMPLING) || defined(XNU_MARBLE_AUDIO_RUNTIME_SAMPLING)
    if(ok)ok=marble_tpl_sampler_prepare(base);
#endif
    print("aud-tpl-qualified: PASS=%d core=%x RestoreTPL_rva=3a40 code_bytes=296 code_digest=%d event_layout=Mu-pinned writes=0 callbacks=0\n",(uint64_t)ok,base,(uint64_t)code);
    return ok;
}
static void marble_tpl_events_capture(unsigned phase)
{
    if(phase>=3 || !marble_tpl_observer.base || marble_tpl_observer.taken[phase])return;
    uint64_t begin=READ_SYSREG(CNTVCT_EL0),freq=READ_SYSREG(CNTFRQ_EL0);
    marble_tpl_observer.taken[phase]=1;
    (void)marble_tpl_events_read(marble_tpl_read,0,marble_tpl_observer.base,&marble_tpl_observer.view[phase]);
    marble_tpl_observer.elapsed[phase]=(READ_SYSREG(CNTVCT_EL0)-begin)*1000000/freq;
}
static void marble_tpl_events_log(void)
{
    for(unsigned phase=0;phase<3;phase++) {
        const MarbleTplView *v=&marble_tpl_observer.view[phase];
        print("aud-tpl-events: phase=%d taken=%d valid=%d pending=%x tpl=%d count=%d truncated=%d snapshot_us=%d event_writes=0 callback_calls=0 timer_changes=0\n",
            (uint64_t)phase,(uint64_t)marble_tpl_observer.taken[phase],(uint64_t)v->valid,v->pending,v->tpl,(uint64_t)v->count,(uint64_t)v->truncated,marble_tpl_observer.elapsed[phase]);
        for(unsigned i=0;i<v->count;i++) {
            const MarbleTplEvent *e=&v->event[i];
            print("aud-tpl-event: phase=%d index=%d event=%x type_signal=%x notify_tpl=%d callback=%x context=%x code=%x,%x,%x,%x,%x,%x,%x,%x\n",
                (uint64_t)phase,(uint64_t)i,e->event,e->type_signal,e->tpl,e->callback,e->context,
                e->code[0],e->code[1],e->code[2],e->code[3],e->code[4],e->code[5],e->code[6],e->code[7]);
        }
    }
}
#endif
