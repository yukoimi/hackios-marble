#include "marble_audio_pins.h"
static MarbleAudioPins marble_tdm_pins;
static unsigned marble_tdm_pins_attempted,marble_tdm_pins_finished;
static int marble_tdm_pins_read(void *cookie,unsigned i,uint32_t *cfg,uint32_t *io)
{(void)cookie;return MARBLE_AUDIO_CLOCK_READ_PIN(i,cfg,io);}
static int marble_tdm_pins_write(void *cookie,unsigned i,unsigned reg,uint32_t value)
{
    (void)cookie;if(i<3 || i>=7 || reg>1 || (reg && (value&~2u)) ||
       (!reg && value!=1 && value!=marble_audio_pins_cfg[i-3]))return 0;
    uint64_t page=0xf100000ULL+4096*marble_audio_clock_guard_pins[i],par=marble_fw_translation(page);
    if((par&1) || (par&0x0000fffffffff000ULL)!=page)return 0;
    uint64_t address=page+4*reg;
    __asm__ volatile("mov w9, %w0\nstr w9, [%1]\ndsb sy"::"r"(value),"r"(address):"x9","memory");return 1;
}
static const MarbleAudioPinOps marble_tdm_pins_ops={0,marble_tdm_pins_read,marble_tdm_pins_write};
static int marble_tdm_pins_snapshot(unsigned phase)
{
    for(unsigned i=0;i<7;i++) {
        uint32_t cfg,io;if(!marble_tdm_pins_read(0,i,&cfg,&io))return 0;
        print("tp: pin phase=%d index=%d gpio=%d cfg=%x io=%x\n",(uint64_t)phase,(uint64_t)i,
            (uint64_t)marble_audio_clock_guard_pins[i],(uint64_t)cfg,(uint64_t)io);
    }
    return 1;
}
static int marble_tdm_pins_begin(void)
{
    if(marble_tdm_pins_attempted)return marble_tdm_pins.active && !marble_tdm_pins.failed;
    marble_tdm_pins_attempted=1;
    print("tp: begin cfg=2c4,2c4,c4,c5 amps_reset=1\n");
    unsigned ok=marble_tdm_pins_snapshot(0) && marble_audio_pins_begin(&marble_tdm_pins_ops,&marble_tdm_pins);
    if(ok)ok=marble_tdm_pins_snapshot(1);
    print("tp: acquired PASS=%d active=%d writes=%d\n",(uint64_t)ok,(uint64_t)marble_tdm_pins.active,(uint64_t)marble_tdm_pins.writes);
    return ok;
}
static int marble_tdm_pins_finish(unsigned stopped)
{
    if(marble_tdm_pins_finished)return marble_tdm_pins.restored;
    if(!marble_tdm_pins_attempted)return 1;
    unsigned ok=marble_audio_pins_end(&marble_tdm_pins_ops,&marble_tdm_pins);
    if(!ok && !stopped) {print("tp: restore_refused active=%d reset_retained=1\n",(uint64_t)marble_tdm_pins.active);return 0;}
    if(ok)ok=marble_tdm_pins_snapshot(2);
    marble_tdm_pins_finished=1;
    print("tp: end PASS=%d active=%d restored=%d writes=%d PAS_stopped=%d amp_changes=0\n",
        (uint64_t)ok,(uint64_t)marble_tdm_pins.active,(uint64_t)marble_tdm_pins.restored,(uint64_t)marble_tdm_pins.writes,(uint64_t)stopped);
    return ok;
}
