/* Passive codec for the exact Mu DxeCore Event.h layout. Caller first pins
 * CoreRestoreTpl code and supplies checked ordinary-RAM reads. No calls to
 * callbacks, list edits, timers, TPL changes, allocations or MMIO here. */
#ifndef MARBLE_TPL_EVENTS_H
#define MARBLE_TPL_EVENTS_H
#include <stdint.h>
#define MARBLE_TPL_MAX_EVENTS 32u
typedef int (*MarbleTplRead)(void *,uint64_t,uint64_t *);
typedef struct {uint64_t event,type_signal,tpl,callback,context,code[8];} MarbleTplEvent;
typedef struct {uint64_t pending,tpl;unsigned valid,count,truncated;MarbleTplEvent event[MARBLE_TPL_MAX_EVENTS];} MarbleTplView;
static int marble_tpl_events_read(MarbleTplRead read,void *cookie,uint64_t base,MarbleTplView *view)
{
    if(!read || !view || (base&4095) || base<0x80000000 || base>0x500000000ULL-0x20000)return 0;
    *view=(MarbleTplView){0};
    if(!read(cookie,base+0x1a318,&view->pending) || !read(cookie,base+0x16630,&view->tpl) || view->tpl>31 || (view->pending&~0x7fffffe0ULL))return 0;
    for(unsigned tpl=5;tpl<=30;tpl++)if(view->pending&(1ULL<<tpl)) {
        uint64_t head=base+0x1a620+16*tpl,first,last,link,previous=head;
        if(!read(cookie,head,&first) || !read(cookie,head+8,&last))return 0;
        link=first;
        while(link!=head) {
            if(view->count==MARBLE_TPL_MAX_EVENTS){view->truncated=1;return 0;}
            if((link&7) || link<0x80000048 || link>0x500000000ULL-16)return 0;
            MarbleTplEvent *e=&view->event[view->count];e->event=link-0x48;
            uint64_t signature,next,back;
            if(!read(cookie,e->event,&signature) || signature!=0x746e7665 ||
               !read(cookie,e->event+8,&e->type_signal) || !(e->type_signal&0x300) ||
               !read(cookie,e->event+0x20,&e->tpl) || e->tpl!=tpl ||
               !read(cookie,e->event+0x28,&e->callback) || !e->callback || (e->callback&3) ||
               !read(cookie,e->event+0x30,&e->context) ||
               !read(cookie,link,&next) || !read(cookie,link+8,&back) || back!=previous)return 0;
            for(unsigned i=0;i<8;i++)if(!read(cookie,e->callback+8*i,&e->code[i]))return 0;
            view->count++;previous=link;link=next;
        }
        uint64_t again,end;
        if(previous!=last || !read(cookie,head,&again) || !read(cookie,head+8,&end) || again!=first || end!=last)return 0;
    }
    uint64_t pending,tpl;
    if(!read(cookie,base+0x1a318,&pending) || pending!=view->pending || !read(cookie,base+0x16630,&tpl) || tpl!=view->tpl)return 0;
    view->valid=1;return 1;
}
#endif
