#ifndef MARBLE_USB_POLL_LEASE_FW_H
#define MARBLE_USB_POLL_LEASE_FW_H
/* Explicit opt-in, original Mu only. Serialize the identified 100ms USB port
 * poll with PAS shutdown, using SetTimer alone. No callback/table patch,
 * interrupt mask, PMIC/charger write, CloseEvent, or removal of pending work.
 * The poll is restarted after the shutdown adapter (including XNU unmap/free)
 * returns. Both the inner PAS and outer metadata-stop 1s limits stay intact.
 * Its next period is rebased; this is not restoration of the old deadline. */
#ifdef XNU_MARBLE_AUDIO_USB_QMP_LEASE
#define MARBLE_USB_POLL_SCOPE "PAS-XNU-unmap-free-QMP-v3"
#else
#define MARBLE_USB_POLL_SCOPE "PAS-XNU-unmap-free-v2"
#endif
static struct {
    uint64_t core,event,callback,usb,period,cancel_status,resume_status,begin,hold_us,resume_trigger;
    unsigned qualified,attempted,cancelled,resume_attempted,restored,failed,late_checked,late_ok;
} marble_usb_poll;
static int marble_usb_poll_binding(void)
{
    uint64_t sig=0,type=0,tpl=0,callback=0,context=1,handle=0;
    uint64_t e=marble_usb_poll.event;
    return e && marble_npa_ram_word(e,&sig) && sig==0x746e7665 &&
        marble_npa_ram_word(e+8,&type) && (uint32_t)type==0x80000200 &&
        marble_npa_ram_word(e+0x20,&tpl) && tpl==8 &&
        marble_npa_ram_word(e+0x28,&callback) && callback==marble_usb_poll.callback &&
        marble_npa_ram_word(e+0x30,&context) && !context &&
        marble_npa_ram_word(marble_usb_poll.usb+0x17e68,&handle) && handle==e;
}
static int marble_usb_poll_qualify(uint64_t core)
{
    static const uint8_t timer_digest[32]={0xe9,0x50,0x34,0x45,0x8d,0x7f,0x29,0xc1,0x42,0xbf,0x4c,0x20,0x2f,0xda,0x3d,0x2e,0x59,0x5d,0x00,0xa7,0xcf,0x20,0x6f,0xae,0xe6,0x81,0xa9,0x96,0xf9,0x71,0x21,0xec};
    static const uint8_t callback_digest[32]={0x88,0x83,0xdf,0x17,0x75,0x80,0x8f,0xb9,0xbd,0xab,0x13,0x23,0xd8,0x80,0x88,0x47,0x7c,0x98,0xa3,0xf1,0xf9,0xa6,0x9e,0xf6,0x9d,0xe1,0x19,0xee,0xeb,0x02,0x5f,0xfd};
    if(marble_usb_poll.qualified || !core || (uint64_t)(uintptr_t)BS->set_timer!=core+0x6394 ||
       !marble_code_digest(marble_npa_ram_word,core+0x6394,0xf4,timer_digest))return 0;
    uint64_t head=core+0x16740,node=0,previous=head;unsigned found=0,count=0;
    if(!marble_npa_ram_word(head,&node))return 0;
    while(node!=head) {
        uint64_t next=0,back=0,sig=0,callback=0;
        if(count++>=32 || node<0x80000098 || (node&7) || !marble_npa_ram_word(node,&next) ||
           !marble_npa_ram_word(node+8,&back) || back!=previous ||
           !marble_npa_ram_word(node-0x98,&sig) || sig!=0x746e7665 ||
           !marble_npa_ram_word(node-0x70,&callback))return 0;
        if(callback>=0x80004f70 && callback<0x500000000ULL && !((callback-0x4f70)&4095) &&
           marble_code_digest(marble_npa_ram_word,callback,0x1f0,callback_digest)) {
            if(found++)return 0;
            marble_usb_poll.event=node-0x98;marble_usb_poll.callback=callback;marble_usb_poll.usb=callback-0x4f70;
        }
        previous=node;node=next;
    }
    uint64_t last=0,period=0;
    if(!found || !marble_npa_ram_word(head+8,&last) || last!=previous || !marble_usb_poll_binding() ||
       !marble_npa_ram_word(marble_usb_poll.event+0xb0,&period) || period!=1000000)return 0;
    marble_usb_poll.core=core;marble_usb_poll.period=period;marble_usb_poll.qualified=1;
    return 1;
}
static int marble_usb_poll_pause(void)
{
    uint64_t tpl=0,notify=1,link=0,period=0;
    if(!marble_usb_poll.qualified || marble_usb_poll.attempted || (uint64_t)(uintptr_t)BS->set_timer!=marble_usb_poll.core+0x6394 || !marble_usb_poll_binding() ||
       !marble_npa_ram_word(marble_usb_poll.core+0x16630,&tpl) || tpl!=8 ||
       !marble_npa_ram_word(marble_usb_poll.event+0x48,&notify) || notify ||
       !marble_npa_ram_word(marble_usb_poll.event+0x98,&link) || !link ||
       !marble_npa_ram_word(marble_usb_poll.event+0xb0,&period) || period!=marble_usb_poll.period)return 0;
    typedef efi_status (EFIAPI *SetTimer)(void *,int,uint64_t);
    marble_usb_poll.attempted=1;marble_usb_poll.begin=READ_SYSREG(CNTVCT_EL0);
    marble_usb_poll.cancel_status=((SetTimer)BS->set_timer)((void *)(uintptr_t)marble_usb_poll.event,0,0);
    uint64_t trigger=1;
    marble_usb_poll.cancelled=!EFI_ERROR(marble_usb_poll.cancel_status) &&
        marble_npa_ram_word(marble_usb_poll.event+0x98,&link) && !link &&
        marble_npa_ram_word(marble_usb_poll.event+0xa8,&trigger) && !trigger &&
        marble_npa_ram_word(marble_usb_poll.event+0xb0,&period) && !period &&
        marble_npa_ram_word(marble_usb_poll.event+0x48,&notify) && !notify;
    if(!marble_usb_poll.cancelled)marble_usb_poll.failed=1;
    return marble_usb_poll.cancelled;
}
static int marble_usb_poll_resume(void)
{
    if(!marble_usb_poll.attempted)return 0;
    if(marble_usb_poll.resume_attempted)return marble_usb_poll.restored;
    marble_usb_poll.resume_attempted=1;
    uint64_t old_tpl=0;
    if((uint64_t)(uintptr_t)BS->set_timer!=marble_usb_poll.core+0x6394 || !marble_usb_poll_binding() ||
       !marble_npa_ram_word(marble_usb_poll.core+0x16630,&old_tpl) || old_tpl!=8){marble_usb_poll.failed=1;return 0;}
    typedef efi_status (EFIAPI *SetTimer)(void *,int,uint64_t);
    marble_usb_poll.resume_status=((SetTimer)BS->set_timer)((void *)(uintptr_t)marble_usb_poll.event,1,marble_usb_poll.period);
    uint64_t link=0,period=0,tpl=0;
    marble_usb_poll.hold_us=(READ_SYSREG(CNTVCT_EL0)-marble_usb_poll.begin)*1000000/READ_SYSREG(CNTFRQ_EL0);
    marble_usb_poll.restored=!EFI_ERROR(marble_usb_poll.resume_status) && marble_usb_poll_binding() &&
        marble_npa_ram_word(marble_usb_poll.event+0x98,&link) && link &&
        marble_npa_ram_word(marble_usb_poll.event+0xb0,&period) && period==1000000 &&
        marble_npa_ram_word(marble_usb_poll.event+0xa8,&marble_usb_poll.resume_trigger) && marble_usb_poll.resume_trigger &&
        marble_npa_ram_word(marble_usb_poll.core+0x16630,&tpl) && tpl==8;
    if(!marble_usb_poll.restored || marble_usb_poll.hold_us>=1000000)marble_usb_poll.failed=1;
    return marble_usb_poll.restored && !marble_usb_poll.failed;
}
static int marble_usb_poll_check_later(void)
{
    if(marble_usb_poll.late_checked)return marble_usb_poll.late_ok;
    marble_usb_poll.late_checked=1;
    uint64_t trigger=0,again=0,period=0,notify=1,link=0;
    for(unsigned i=0;i<8;i++) {
        if(marble_usb_poll.restored && !marble_usb_poll.failed && marble_usb_poll_binding() &&
           marble_npa_ram_word(marble_usb_poll.event+0xa8,&trigger) && trigger>marble_usb_poll.resume_trigger &&
           marble_npa_ram_word(marble_usb_poll.event+0xb0,&period) && period==marble_usb_poll.period &&
           marble_npa_ram_word(marble_usb_poll.event+0x98,&link) && link &&
           marble_npa_ram_word(marble_usb_poll.event+0x48,&notify) && !notify &&
           marble_npa_ram_word(marble_usb_poll.event+0xa8,&again) && again==trigger) {marble_usb_poll.late_ok=1;break;}
    }
    print("aud-usb-poll-later: PASS=%d checked=1 binding=%d period_100ns=%d resume_trigger=%x later_trigger=%x notify_link=%x timer_link=%x scheduler_progress=%d timer_writes=0 callback_calls=0\n",
        (uint64_t)marble_usb_poll.late_ok,(uint64_t)marble_usb_poll_binding(),period,marble_usb_poll.resume_trigger,trigger,notify,link,(uint64_t)marble_usb_poll.late_ok);
    return marble_usb_poll.late_ok;
}
static int marble_usb_poll_complete(void)
{
    /* This runs in the existing metadata-stop transaction, before its end
     * timestamp. No work is moved outside either original timing boundary. */
    uint64_t tpl=0;
    if(!BS->raise_tpl || !BS->restore_tpl || !marble_npa_ram_word(marble_usb_poll.core+0x16630,&tpl) || (tpl!=4 && tpl!=8))return 0;
    typedef uint64_t (EFIAPI *Raise)(uint64_t);
    typedef void (EFIAPI *Restore)(uint64_t);
    uint64_t old=((Raise)BS->raise_tpl)(8);
    int ok=marble_usb_poll_resume();
    ((Restore)BS->restore_tpl)(old);
    return ok;
}
static void marble_usb_poll_log(void)
{
    print("aud-usb-poll-lease: scope=%s qualified=%d attempted=%d cancelled=%d resume_attempted=%d restored=%d failed=%d event=%x callback=%x period_100ns=%d hold_us=%d cancel_status=%x resume_status=%x cadence_rebased=1 timer_API_only=1 CloseEvent_calls=0 pending_removals=0 charger_writes=0 IRQ_changes=0 full_PAS_limit_us=1000000\n",
        MARBLE_USB_POLL_SCOPE,(uint64_t)marble_usb_poll.qualified,(uint64_t)marble_usb_poll.attempted,(uint64_t)marble_usb_poll.cancelled,
        (uint64_t)marble_usb_poll.resume_attempted,(uint64_t)marble_usb_poll.restored,(uint64_t)marble_usb_poll.failed,
        marble_usb_poll.event,marble_usb_poll.callback,marble_usb_poll.period,marble_usb_poll.hold_us,marble_usb_poll.cancel_status,marble_usb_poll.resume_status);
}
#endif
