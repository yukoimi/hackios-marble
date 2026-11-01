#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../xnuloader/marble_audio_map.h"
static uint32_t mmio[0x100000/4];static uint64_t debug[7];
static unsigned stuck,wrong,wake_fail,route_writes,trigger_seen;
static void print(const char *s,...){(void)s;}
static void stall(unsigned us){assert(us<=5);}
static struct {void (*stall)(unsigned);} boot={stall},*BS=&boot;
static int marble_audio_map_barrier(void *p){(void)p;return 0;}
static int marble_audio_tbu_mapping(void){return 1;}
static int marble_audio_tbu_read32(void *p,size_t at,uint32_t *v)
{(void)p;assert(at==0x20);*v=(debug[0]&0x10000?2:0)|(((debug[3]&&stuck)||(trigger_seen&&stuck==2))?1:0);return 0;}
static int marble_audio_tbu_read64(void *p,size_t at,uint64_t *v)
{(void)p;assert(at<=0x30 && !(at&7));*v=debug[at/8];return 0;}
static int marble_audio_tbu_write64(void *,size_t,uint64_t);
#define MARBLE_AUDIO_TBU_TEST_IO 1
#include "../../xnuloader/marble_audio_tbu_fw.h"
static int marble_audio_tbu_write64(void *p,size_t at,uint64_t v)
{
    (void)p;assert(marble_tbu_write_allowed(at,8));marble_audio_tbu_writes++;debug[at/8]=v;
    if(at==0x18 && v) {
        trigger_seen=1;
        assert(debug[0]==0x80192001 && v==0xbd && debug[1]==0x10000000);
        assert(mmio[0x814/4]==0x80001801 && mmio[0xc14/4]==5);
        debug[5]=(wrong?0xdead0000ULL:0x90003000ULL)<<12;
    }
    return 0;
}
static int read32(void *p,size_t at,uint32_t *v)
{(void)p;assert(at<0x100000 && !(at&3));*v=mmio[at/4];return 0;}
static int write32(void *p,size_t at,uint32_t v)
{
    (void)p;assert(at==0x814 || at==0xc14 || at==0x75f8);
    mmio[at/4]=v;
    if(at==0x75f8 && !wake_fail)mmio[0x75fc/4]=v;
    if(at!=0x75f8)route_writes++;
    return 0;
}
static MarbleAudioDmaSnapshot setup(void)
{
    memset(mmio,0,sizeof(mmio));memset(debug,0,sizeof(debug));stuck=wrong=wake_fail=route_writes=trigger_seen=0;
    marble_audio_stream_writes=marble_audio_tbu_writes=0;
    MarbleAudioDmaSnapshot s={.scr0=0x220406,.id0=0x4c017e71,.id1=0x6000004b};
    mmio[0]=s.scr0;mmio[0x20/4]=s.id0;mmio[0x24/4]=s.id1;
    for(unsigned i=0;i<5;i++){s.smr[i]=0x80000000|i;s.s2cr[i]=i;mmio[0x800/4+i]=s.smr[i];mmio[0xc00/4+i]=i;}
    s.smr[5]=mmio[0x814/4]=0x110584;s.s2cr[5]=mmio[0xc14/4]=0x20005;
    mmio[0x75f8/4]=mmio[0x75fc/4]=2;debug[0]=0x80182000;
    return s;
}
int main(void)
{
    ArmSmmu4KIo io={0,0x100000,read32,0,write32,0,marble_audio_map_barrier};
    uint64_t par;unsigned retain;MarbleAudioDmaSnapshot s=setup();
    assert(!marble_audio_tbu_translate(0,&io,&s,5,0x90003000,&par,&retain));
    assert(!retain && route_writes==4 && marble_audio_stream_writes==4 && (par>>12)==0x90003000);
    assert(mmio[0x814/4]==s.smr[5] && mmio[0xc14/4]==s.s2cr[5] && mmio[0x75f8/4]==2 && debug[0]==0x80182000 && !debug[3]);
    s=setup();wrong=1;assert(marble_audio_tbu_translate(0,&io,&s,5,0x90003000,&par,&retain) && !retain && route_writes==4);
    s=setup();debug[0]|=1;assert(marble_audio_tbu_translate(0,&io,&s,5,0x90003000,&par,&retain) && !retain && !route_writes && mmio[0x75f8/4]==2);
    s=setup();stuck=2;assert(marble_audio_tbu_translate(0,&io,&s,5,0x90003000,&par,&retain) && retain && route_writes==2 && mmio[0x75f8/4]==66);
    s=setup();wake_fail=1;assert(marble_audio_tbu_translate(0,&io,&s,5,0x90003000,&par,&retain) && retain && !route_writes);
    puts("PASS actual LPASS wrapper: micro-idle bit preservation, exact native SMR/S2CR restoration, ECATS SID1, wrong PA, busy, timeout retention");
}
