#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../xnuloader/efi.h"
static uint64_t clock_us;
static uint64_t cnt(void){return clock_us;}
#define MARBLE_TPL_SAMPLER_SP() 0x83000000ULL
#define READ_SYSREG(name) read_##name()
#define read_CNTVCT_EL0() cnt()
#define read_CNTFRQ_EL0() 1000000u
static void print(const char *fmt,...){(void)fmt;}
static unsigned creates,arms,cancels,closes,fail_arm,fail_cancel,fail_close,ram_reads;
static void (*notify)(void *,void *);
static efi_status create(uint32_t type,uint64_t tpl,void (*fn)(void *,void *),const void *ctx,const efi_guid *group,void **event)
{assert(type==0x80000200 && tpl==16 && !ctx && !group);creates++;notify=fn;*event=(void *)0x1234;return 0;}
static efi_status timer(void *event,int kind,uint64_t period)
{assert(event==(void *)0x1234);if(kind==1){assert(period==100000);arms++;return fail_arm?EFI_ERROR_BIT|7:0;}assert(!kind && !period);cancels++;return fail_cancel?EFI_ERROR_BIT|7:0;}
static efi_status close_event(void *event){assert(event==(void *)0x1234);closes++;return fail_close?EFI_ERROR_BIT|7:0;}
static efi_boot_services table={.create_event_ex=create,.set_timer=(void *)timer,.close_event=close_event},*BS=&table;
static int marble_npa_ram_word(uint64_t at,uint64_t *value)
{ram_reads++;if(at==0x8001a318)*value=0x100;else if(at==0x8001a6a0)*value=0x81000048;else if(at==0x81000000)*value=0x746e7665;else if(at==0x81000028)*value=0x82001000;else if(at>=0x83000000 && at<0x83000800)*value=at==0x83000000?0xf9001000:0;else if(at>=0xf9001000 && at<0xf9001040)*value=at;else return 0;return 1;}
#include "../../xnuloader/marble_tpl_sampler_fw.h"
static void reset(void){memset(&marble_tpl_sampler,0,sizeof(marble_tpl_sampler));creates=arms=cancels=closes=fail_arm=fail_cancel=fail_close=ram_reads=0;clock_us=0;notify=0;}
int main(void)
{
    reset();assert(marble_tpl_sampler_release() && !creates && !cancels && !closes);
    assert(!marble_tpl_sampler_prepare(0));assert(marble_tpl_sampler_prepare(0x80000000));assert(!marble_tpl_sampler_prepare(0x80000000));
    assert(marble_tpl_sampler_arm());assert(creates==1 && arms==1 && marble_tpl_sampler.active);
    clock_us=10000;notify((void *)0x1234,0);assert(marble_tpl_sampler.count==1 && marble_tpl_sampler.sample[0].valid && marble_tpl_sampler.sample[0].callback==0x82001000);
    unsigned reads=ram_reads;notify((void *)0x5678,0);assert(ram_reads==reads);
    for(unsigned i=0;i<130;i++){clock_us+=10000;notify((void *)0x1234,0);}assert(marble_tpl_sampler.count==128 && marble_tpl_sampler.truncated);
    assert(marble_tpl_sampler.stacks==2);
    for(unsigned s=0;s<2;s++){assert(marble_tpl_sampler.stack[s].words==256);assert(marble_tpl_sampler.stack[s].word[0]==0xf9001000);assert(!marble_tpl_sampler.stack[s].word[1]);}
    assert(marble_tpl_sampler_release() && cancels==1 && closes==1 && !marble_tpl_sampler.active && !marble_tpl_sampler.event);
    reads=ram_reads;notify((void *)0x1234,0);assert(reads==ram_reads);assert(marble_tpl_sampler_release() && cancels==1 && closes==1);marble_tpl_sampler_log();
    reset();assert(marble_tpl_sampler_prepare(0x80000000));fail_arm=1;assert(!marble_tpl_sampler_arm() && !marble_tpl_sampler.active);assert(marble_tpl_sampler_release() && closes==1);
    reset();assert(marble_tpl_sampler_prepare(0x80000000));assert(marble_tpl_sampler_arm());fail_cancel=1;assert(!marble_tpl_sampler_release() && !marble_tpl_sampler.active && !closes);assert(!marble_tpl_sampler_release() && cancels==1);
    reset();assert(marble_tpl_sampler_prepare(0x80000000));assert(marble_tpl_sampler_arm());fail_close=1;assert(!marble_tpl_sampler_release() && !marble_tpl_sampler.active && marble_tpl_sampler.event);assert(!marble_tpl_sampler_release() && closes==1);
    puts("PASS own TPL software timer: exact 10ms/TPL16 ABI, RAM-only callback, wrong-handle/inactive guards, bounded128, cancel+close exactly once, retained failures; no hardware execution");
    return 0;
}
