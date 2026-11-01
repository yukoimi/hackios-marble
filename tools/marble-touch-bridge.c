/* Static Darwin process: read ONLY the raw RAM support disk; inject HID input.
 * /dev/rmd1 is a kernel RAM device, unrelated to Android userdata.
 */
#include "mini.h"
#include "../xnuloader/marble_touch_mailbox.h"
static struct mt_mailbox box;
static struct { unsigned down,x,y; uint64_t source_down, posted_down; } fingers[10];
static int sink;
static unsigned delivered;
static unsigned buttons[4];
static uint64_t ticks(void) { uint64_t t; __asm__ volatile("mrs %0, cntvct_el0" : "=r"(t)); return t; }
static int post(unsigned type,unsigned id,unsigned x,unsigned y) {
    struct { uint64_t time; uint32_t type,finger,x,y; } t={0,type,id,x,y};
    if(write(sink,&t,sizeof(t))!=sizeof(t)) return -1;
    fingers[id].down=type!=3; fingers[id].x=x; fingers[id].y=y;
    delivered++;
    return 0;
}
static int post_button(unsigned type,unsigned id) {
    struct { uint64_t time; uint32_t type,finger,x,y; } t={0,type,id,0,0};
    if(id<1 || id>3 || write(sink,&t,sizeof(t))!=sizeof(t))return -1;
    buttons[id]=type==4;delivered++;
    printf("marble-buttons: bridge type=%u key=%u\n",type,id);
    return 0;
}
static void release_all(void) {
    for(unsigned i=0;i<10;i++) if(fingers[i].down) post(3,i,fingers[i].x,fingers[i].y);
    for(unsigned i=1;i<=3;i++)if(buttons[i])post_button(5,i);
}
int main(int argc,char **argv,char **envp) {
    (void)argc; (void)argv; (void)envp;
    int disk=open("/dev/rmd1",O_RDONLY,0);
    sink=open("/dev/redwood-touch",O_WRONLY,0);
    if(disk<0 || sink<0) { printf("marble-touch: RAM/input open failed errno=%d\n",errno); return 1; }
    if(argc==2 && strcmp(argv[1],"--selftest")==0) {
        struct mt_mailbox first;
        if(pread(disk,&first,sizeof(first),MT_MAILBOX_OFFSET)!=sizeof(first)) return 4;
        usleep(100000);
        if(pread(disk,&box,sizeof(box),MT_MAILBOX_OFFSET)!=sizeof(box)) return 5;
        int ok=first.magic==MT_MAILBOX_MAGIC && box.magic==MT_MAILBOX_MAGIC &&
            (box.version==1 || box.version==2) && box.event_size==32 && box.capacity==MT_CAPACITY && box.heartbeat>first.heartbeat;
        printf("marble-touch: RAM mailbox selftest %s heartbeat=%llu->%llu ready=%u\n",ok?"PASS":"FAIL",first.heartbeat,box.heartbeat,box.ready);
        return ok?0:6;
    }
    /* Interactive launchd job prevents background timer coalescing. Give this
     * short input pump priority over CPU rendering; always sleep between polls. */
    long priority=mini_syscall(96,0,0,-20,0,0,0); /* setpriority(PRIO_PROCESS, self, -20) */
    uint64_t cursor=0, last_heartbeat=0, last_log=ticks(), max_lag=0;
    unsigned started=0, stale=0, coalesced=0, gestures=0;
    printf("marble-touch: bridge started; raw rmd1 offset 0x%x priority=%ld errno=%d\n",MT_MAILBOX_OFFSET,priority,errno);
    for(;;) {
        struct mt_mailbox after;
        if(pread(disk,&box,sizeof(box),MT_MAILBOX_OFFSET)!=sizeof(box) ||
           pread(disk,&after,64,MT_MAILBOX_OFFSET)!=64) {
            release_all(); printf("marble-touch: RAM read error errno=%d\n",errno); return 2;
        }
        if((box.heartbeat&1) || box.heartbeat!=after.heartbeat) {usleep(1000);continue;}
        if(box.magic!=MT_MAILBOX_MAGIC || (box.version!=1 && box.version!=2) ||
           box.event_size!=sizeof(struct mt_event) || box.capacity!=MT_CAPACITY) {
            release_all(); printf("marble-touch: invalid RAM mailbox errno=%d\n",errno); return 2;
        }
        if(!started) {cursor=box.produced; started=1; printf("marble-touch: mailbox online ready=%u heartbeat=%llu\n",box.ready,box.heartbeat);}
        if(box.heartbeat==last_heartbeat) stale++; else {stale=0;last_heartbeat=box.heartbeat;}
        if(stale==200) {release_all(); printf("marble-touch: producer stale\n");}
        if(box.produced<cursor || box.produced-cursor>MT_CAPACITY) {
            release_all(); cursor=box.produced;
            printf("marble-touch: overrun, released all contacts\n");
        }
        while(cursor<box.produced) {
            uint64_t next=cursor+1;
            const struct mt_event *e=&box.events[(next-1)%MT_CAPACITY];
            if(e->sequence!=next || e->type<1 || e->type>(box.version==2?5u:3u) || e->finger>=10 || e->x>65535 || e->y>65535) {
                release_all(); cursor=box.produced; break;
            }
            if(e->type>=4) {
                if(e->finger<1 || e->finger>3 || e->x || e->y) {release_all();cursor=box.produced;break;}
                if(e->type==4 || buttons[e->finger]) {
                    if(post_button(e->type,e->finger)) {release_all();return 3;}
                }
                cursor=next;continue;
            }
            if(e->type==2 && next<box.produced) {
                const struct mt_event *following=&box.events[next%MT_CAPACITY];
                if(following->sequence==next+1 && following->type==2 && following->finger==e->finger) {
                    coalesced++; cursor=next; continue;
                }
            }
            uint64_t now=ticks();
            if(now>=e->ticks && now-e->ticks>max_lag) max_lag=now-e->ticks;
            /* A new consumer must see DOWN before MOVE/UP. */
            if(e->type==1 || fingers[e->finger].down) {
                if(post(e->type,e->finger,e->x,e->y)) { release_all(); return 3; }
                if(e->type==1) {
                    fingers[e->finger].source_down=e->ticks;
                    fingers[e->finger].posted_down=now;
                    gestures++;
                } else if(e->type==3 && gestures<=256) {
                    uint64_t origin=fingers[e->finger].source_down, posted=fingers[e->finger].posted_down;
                    printf("marble-touch: UP finger=%u source_hold_ms=%llu posted_hold_ms=%llu release_lag_ms=%llu\n",e->finger,
                           e->ticks>=origin?(e->ticks-origin)/19200:0, now>=posted?(now-posted)/19200:0,
                           now>=e->ticks?(now-e->ticks)/19200:0);
                }
            }
            cursor=next;
        }
        if(ticks()-last_log>=19200000ULL*5) {
            printf("marble-touch: delivered=%u producer=%llu coalesced=%u max_lag_ms=%llu polls=%llu reports=%llu bad=%llu ready=%u\n",delivered,box.produced,coalesced,max_lag/19200,box.polls,box.reports,box.bad,box.ready);
            last_log=ticks(); max_lag=0;
        }
        usleep(4000);
    }
}
