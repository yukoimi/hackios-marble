#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../xnuloader/efi.h"
static uint64_t core,clock_us,period,tpl,notify,callback,context,link,trigger,handle;
static unsigned cancels,resumes,fail_cancel,fail_resume,fail_digest,reads;
static efi_status timer(void *event,int kind,uint64_t value)
{
    assert(event==(void *)0xa0000000);
    if(kind==0){assert(!value);cancels++;period=link=trigger=0;return fail_cancel?EFI_ERROR_BIT|7:0;}
    assert(kind==1 && value==1000000);resumes++;period=value;link=core+0x16740;trigger=clock_us+100000;return fail_resume?EFI_ERROR_BIT|7:0;
}
static uint64_t raise(uint64_t next){uint64_t old=tpl;tpl=next;return old;}
static void restore(uint64_t old){tpl=old;}
static efi_boot_services table={.set_timer=(void *)timer,.raise_tpl=(void *)raise,.restore_tpl=(void *)restore},*BS=&table;
static int marble_npa_ram_word(uint64_t at,uint64_t *v)
{
    reads++;
    if(at==core+0x16740 || at==core+0x16748)*v=0xa0000098;
    else if(at==core+0x16630)*v=tpl;
    else if(at==0xa0000000)*v=0x746e7665;
    else if(at==0xa0000008)*v=0x80000200;
    else if(at==0xa0000020)*v=8;
    else if(at==0xa0000028)*v=callback;
    else if(at==0xa0000030)*v=context;
    else if(at==0xa0000048)*v=notify;
    else if(at==0xa0000098)*v=link;
    else if(at==0xa00000a0)*v=core+0x16740;
    else if(at==0xa00000a8)*v=trigger;
    else if(at==0xa00000b0)*v=period;
    else if(at==0x90017e68)*v=handle;
    else return 0;
    return 1;
}
static int marble_code_digest(int (*read)(uint64_t,uint64_t *),uint64_t at,unsigned bytes,const uint8_t *digest)
{assert(read==marble_npa_ram_word && digest);assert((at==core+0x6394 && bytes==0xf4) || (at==0x90004f70 && bytes==0x1f0));return !fail_digest;}
#define READ_SYSREG(name) read_##name()
#define read_CNTVCT_EL0() clock_us
#define read_CNTFRQ_EL0() 1000000u
static void print(const char *fmt,...){(void)fmt;}
#include "../../xnuloader/marble_usb_poll_lease_fw.h"
static void reset(void)
{
    memset(&marble_usb_poll,0,sizeof(marble_usb_poll));core=(uint64_t)(uintptr_t)timer-0x6394;
    clock_us=cancels=resumes=fail_cancel=fail_resume=fail_digest=reads=0;
    period=1000000;tpl=8;notify=context=0;callback=0x90004f70;link=core+0x16740;trigger=100000;handle=0xa0000000;
}
int main(void)
{
    reset();assert(marble_usb_poll_qualify(core));assert(cancels==0 && resumes==0);
    assert(marble_usb_poll_pause());assert(!marble_usb_poll_pause());clock_us=608000;
    tpl=4;assert(marble_usb_poll_complete() && tpl==4 && marble_usb_poll.restored && !marble_usb_poll.failed);tpl=8;
    assert(marble_usb_poll_resume() && cancels==1 && resumes==1 && period==1000000);marble_usb_poll_log();
    trigger+=1000000;assert(marble_usb_poll_check_later() && marble_usb_poll_check_later());assert(cancels==1 && resumes==1);
    reset();assert(marble_usb_poll_qualify(core) && marble_usb_poll_pause() && marble_usb_poll_resume());
    assert(!marble_usb_poll_check_later());
    reset();assert(marble_usb_poll_qualify(core) && marble_usb_poll_pause() && marble_usb_poll_resume());
    trigger+=1000000;notify=0xa0000048;assert(!marble_usb_poll_check_later());
    for(unsigned i=0;i<4;i++) {
        reset();if(i==0)fail_digest=1;if(i==1)period=2000000;if(i==2)context=1;if(i==3)handle=0;
        assert(!marble_usb_poll_qualify(core) && !cancels && !resumes);
    }
    for(unsigned i=0;i<3;i++) {
        reset();assert(marble_usb_poll_qualify(core));if(i==0)notify=0xa0000048;if(i==1)tpl=4;if(i==2)callback=0x90004f74;
        assert(!marble_usb_poll_pause() && !marble_usb_poll_resume() && !cancels && !resumes);
    }
    reset();assert(marble_usb_poll_qualify(core));fail_cancel=1;assert(!marble_usb_poll_pause());clock_us=608000;
    assert(!marble_usb_poll_resume() && marble_usb_poll.restored && marble_usb_poll.failed && resumes==1);
    reset();assert(marble_usb_poll_qualify(core) && marble_usb_poll_pause());fail_resume=1;
    assert(!marble_usb_poll_resume() && marble_usb_poll.failed);assert(!marble_usb_poll_resume() && resumes==1);
    reset();assert(marble_usb_poll_qualify(core) && marble_usb_poll_pause());callback=0;
    assert(!marble_usb_poll_resume() && marble_usb_poll.failed && !resumes);
    reset();assert(marble_usb_poll_qualify(core) && marble_usb_poll_pause());clock_us=1000000;
    assert(!marble_usb_poll_resume() && marble_usb_poll.restored && marble_usb_poll.failed);
    puts("PASS pinned USB poll lease model: readonly qualification, exact100ms period, TPL8 and no-pending guard, API cancel/restart once, partial-failure restoration, changed-binding refusal, original1s bound; no hardware execution");
}
