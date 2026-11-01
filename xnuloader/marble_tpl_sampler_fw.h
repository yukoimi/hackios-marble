#ifndef MARBLE_TPL_SAMPLER_FW_H
#define MARBLE_TPL_SAMPLER_FW_H
/* Own, explicitly opted-in software timer. Its callback only samples RAM;
 * no foreign callback, event, controller, power, IRQ or timer state is edited. */
static struct {
    void *event;uint64_t base,begin,freq,arm_status,cancel_status,close_status,release_us;
    volatile unsigned active,count;
    unsigned prepared,armed,release_attempted,released,truncated;
    struct {uint64_t at_us,pending,next,callback;unsigned valid;} sample[128];
    struct {uint64_t sp,word[256];unsigned index,words;} stack[2];
    unsigned stacks;
} marble_tpl_sampler;
#ifndef MARBLE_TPL_SAMPLER_SP
static uint64_t marble_tpl_sampler_sp(void)
{uint64_t sp;__asm__ volatile("mov %0, sp":"=r"(sp));return sp;}
#define MARBLE_TPL_SAMPLER_SP() marble_tpl_sampler_sp()
#endif
/* Two bounded snapshots of our own stack. Code-prefix inspection is deferred
 * until logging, outside the timer callback. These are raw stack words,
 * not a validated unwind or proof of an active foreign callback. */
static void marble_tpl_sampler_stack(unsigned index)
{
#ifdef XNU_MARBLE_AUDIO_RUNTIME_SAMPLING
    if(marble_tpl_sampler.stacks>=2 || (index!=59 && index!=89))return;
#else
    if(marble_tpl_sampler.stacks>=2 || (index!=0 && index!=39))return;
#endif
    unsigned slot=marble_tpl_sampler.stacks++;uint64_t sp=MARBLE_TPL_SAMPLER_SP();
    marble_tpl_sampler.stack[slot].index=index;marble_tpl_sampler.stack[slot].sp=sp;
    if((sp&15) || sp<0x80000000 || sp>0x4fffff800ULL)return;
    for(unsigned i=0;i<256;i++) {
        uint64_t word=0;
        if(!marble_npa_ram_word(sp+8*i,&word))break;
        marble_tpl_sampler.stack[slot].word[i]=word;marble_tpl_sampler.stack[slot].words=i+1;
    }
}
static void EFIAPI marble_tpl_sample_callback(void *event,void *context)
{
    (void)context;
    if(!marble_tpl_sampler.active || event!=marble_tpl_sampler.event)return;
    unsigned index=marble_tpl_sampler.count;
    if(index>=128){marble_tpl_sampler.truncated=1;return;}
    uint64_t now=READ_SYSREG(CNTVCT_EL0),head=marble_tpl_sampler.base+0x1a6a0;
    uint64_t pending=0,next=0,signature=0,callback=0;
    unsigned valid=marble_npa_ram_word(marble_tpl_sampler.base+0x1a318,&pending) && marble_npa_ram_word(head,&next);
    if(valid && next!=head)valid=next>=0x80000048 && !(next&7) &&
        marble_npa_ram_word(next-0x48,&signature) && signature==0x746e7665 &&
        marble_npa_ram_word(next-0x20,&callback);
    marble_tpl_sampler.sample[index].at_us=(now-marble_tpl_sampler.begin)*1000000/marble_tpl_sampler.freq;
    marble_tpl_sampler.sample[index].pending=pending;marble_tpl_sampler.sample[index].next=next;
    marble_tpl_sampler.sample[index].callback=callback;marble_tpl_sampler.sample[index].valid=valid;
    marble_tpl_sampler_stack(index);
    __asm__ volatile("":::"memory");marble_tpl_sampler.count=index+1;
}
static int marble_tpl_sampler_prepare(uint64_t base)
{
    if(marble_tpl_sampler.prepared || !base || !BS->create_event_ex || !BS->set_timer || !BS->close_event)return 0;
    efi_status status=BS->create_event_ex(0x80000200u,16,marble_tpl_sample_callback,0,0,&marble_tpl_sampler.event);
    if(EFI_ERROR(status) || !marble_tpl_sampler.event)return 0;
    marble_tpl_sampler.base=base;marble_tpl_sampler.prepared=1;return 1;
}
static int marble_tpl_sampler_arm(void)
{
    if(!marble_tpl_sampler.prepared || marble_tpl_sampler.armed || marble_tpl_sampler.release_attempted)return 0;
    typedef efi_status (EFIAPI *SetTimer)(void *,int,uint64_t);
    marble_tpl_sampler.freq=READ_SYSREG(CNTFRQ_EL0);if(!marble_tpl_sampler.freq)return 0;
    marble_tpl_sampler.begin=READ_SYSREG(CNTVCT_EL0);marble_tpl_sampler.active=1;
    marble_tpl_sampler.arm_status=((SetTimer)BS->set_timer)(marble_tpl_sampler.event,1,100000); /* software 10ms */
    if(EFI_ERROR(marble_tpl_sampler.arm_status)){marble_tpl_sampler.active=0;return 0;}
    marble_tpl_sampler.armed=1;return 1;
}
static int marble_tpl_sampler_release(void)
{
    if(!marble_tpl_sampler.prepared)return 1;
    if(marble_tpl_sampler.release_attempted)return marble_tpl_sampler.released;
    marble_tpl_sampler.release_attempted=1;marble_tpl_sampler.active=0;
    typedef efi_status (EFIAPI *SetTimer)(void *,int,uint64_t);
    uint64_t begin=READ_SYSREG(CNTVCT_EL0),freq=READ_SYSREG(CNTFRQ_EL0);
    marble_tpl_sampler.cancel_status=((SetTimer)BS->set_timer)(marble_tpl_sampler.event,0,0);
    if(!EFI_ERROR(marble_tpl_sampler.cancel_status))marble_tpl_sampler.close_status=BS->close_event(marble_tpl_sampler.event);
    else marble_tpl_sampler.close_status=EFI_ERROR_BIT|7;
    marble_tpl_sampler.released=!EFI_ERROR(marble_tpl_sampler.cancel_status) && !EFI_ERROR(marble_tpl_sampler.close_status);
    if(marble_tpl_sampler.released)marble_tpl_sampler.event=0;
    marble_tpl_sampler.release_us=(READ_SYSREG(CNTVCT_EL0)-begin)*1000000/freq;
    return marble_tpl_sampler.released;
}
static void marble_tpl_sampler_log(void)
{
    print("aud-tpl-sampler: prepared=%d armed=%d released=%d active=%d count=%d truncated=%d arm_status=%x cancel_status=%x close_status=%x release_us=%d period_100ns=100000 notify_tpl=16 foreign_timer_changes=0 callback_IO=0\n",
        (uint64_t)marble_tpl_sampler.prepared,(uint64_t)marble_tpl_sampler.armed,(uint64_t)marble_tpl_sampler.released,
        (uint64_t)marble_tpl_sampler.active,(uint64_t)marble_tpl_sampler.count,(uint64_t)marble_tpl_sampler.truncated,
        marble_tpl_sampler.arm_status,marble_tpl_sampler.cancel_status,marble_tpl_sampler.close_status,marble_tpl_sampler.release_us);
    for(unsigned i=0;i<marble_tpl_sampler.count;i++)print("aud-tpl-sample: index=%d at_us=%d valid=%d pending=%x next=%x next_callback=%x\n",
        (uint64_t)i,marble_tpl_sampler.sample[i].at_us,(uint64_t)marble_tpl_sampler.sample[i].valid,
        marble_tpl_sampler.sample[i].pending,marble_tpl_sampler.sample[i].next,marble_tpl_sampler.sample[i].callback);
    for(unsigned s=0;s<marble_tpl_sampler.stacks;s++) {
        print("aud-tpl-stack: sample=%d sp=%x words=%d raw_only=1 foreign_calls=0\n",(uint64_t)marble_tpl_sampler.stack[s].index,
            marble_tpl_sampler.stack[s].sp,(uint64_t)marble_tpl_sampler.stack[s].words);
        for(unsigned i=0;i<marble_tpl_sampler.stack[s].words;i++) {
            print("aud-tpl-stack-word: sample=%d offset=%d value=%x\n",(uint64_t)marble_tpl_sampler.stack[s].index,(uint64_t)(8*i),marble_tpl_sampler.stack[s].word[i]);
            uint64_t word=marble_tpl_sampler.stack[s].word[i],c[8]={0};unsigned valid=1;
            if((word&3) || word<0xf8000000 || word>0xffffffc0)continue;
            for(unsigned j=0;j<8;j++)if(!marble_npa_ram_word(word+8*j,&c[j])){valid=0;break;}
            if(valid) {
                print("aud-tpl-stack-code: sample=%d offset=%d callback=%x code=%x,%x,%x,%x,%x,%x,%x,%x\n",(uint64_t)marble_tpl_sampler.stack[s].index,(uint64_t)(8*i),
                    marble_tpl_sampler.stack[s].word[i],c[0],c[1],c[2],c[3],c[4],c[5],c[6],c[7]);
            }
        }
    }
}
#endif
