/* Passive board boundary shared by isolated clock and silent TDM lifecycle. */
static uint32_t marble_audio_clock_saved[14];
#ifndef MARBLE_AUDIO_CLOCK_READ_PIN
static int marble_audio_clock_read_pin(unsigned i,uint32_t *cfg,uint32_t *io)
{
    uint64_t address=0xf100000ULL+0x1000ULL*marble_audio_clock_guard_pins[i];
    uint64_t par=marble_fw_translation(address);
    if((par&1) || (par&0x0000fffffffff000ULL)!=address)return 0;
    *cfg=*(volatile uint32_t *)(uintptr_t)address;
    *io=*(volatile uint32_t *)(uintptr_t)(address+4);
    __asm__ volatile("dmb sy" ::: "memory");return 1;
}
#define MARBLE_AUDIO_CLOCK_READ_PIN marble_audio_clock_read_pin
#endif
static __attribute__((noinline)) int marble_audio_clock_guard(unsigned after)
{
    unsigned ok=1;
    for(unsigned i=0;i<7;i++) {
        uint32_t cfg,io;
        if(!MARBLE_AUDIO_CLOCK_READ_PIN(i,&cfg,&io))return 0;
        if(!marble_audio_clock_pin_valid(i,cfg,io))ok=0;
        if(after) {if(cfg!=marble_audio_clock_saved[2*i] || ((io^marble_audio_clock_saved[2*i+1])&2))ok=0;}
        else {marble_audio_clock_saved[2*i]=cfg;marble_audio_clock_saved[2*i+1]=io;}
        print("prm: pin after=%d index=%d cfg=%x io=%x\n",(uint64_t)after,(uint64_t)i,(uint64_t)cfg,(uint64_t)io);
    }
    print("prm: guard after=%d PASS=%d writes=0\n",(uint64_t)after,(uint64_t)ok);return ok;
}
