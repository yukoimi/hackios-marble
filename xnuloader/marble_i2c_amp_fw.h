/* Default ID-only bridge. Explicit isolated profiles add reset-state reads
 * and a minimum-volume write/readback; no speaker enable, TDM or calibration.
 * Caller holds the verified 100kHz/configuration clock leases. */
#ifndef MARBLE_I2C_AMP_FW_H
#define MARBLE_I2C_AMP_FW_H
#include "marble_i2c_fifo.h"
#ifdef XNU_MARBLE_AMP_REGISTERS_ONLY
#include "marble_aw2032_registers.h"
static unsigned marble_i2c_amp_registers_requested,marble_i2c_amp_register,marble_i2c_amp_register_dirty;
static uint16_t marble_i2c_amp_sysctrl,marble_i2c_amp_sysctrl2;
#ifdef XNU_MARBLE_AMP_PROFILE_ONLY
#include "marble_aw2032_profile.h"
static unsigned marble_i2c_amp_profile_requested,marble_i2c_amp_profile_readback;
static MarbleAw2032Pair marble_i2c_amp_profile_pair;
static uint16_t marble_i2c_amp_efrh,marble_i2c_amp_efrm1;
#ifdef XNU_MARBLE_AUDIO_DIGITAL_PLL
#include "marble_aw2032_digital_fifo.h"
static unsigned marble_i2c_amp_digital_reg,marble_i2c_amp_digital_readback;
static unsigned marble_i2c_amp_digital_authorized;
static uint16_t marble_i2c_amp_digital_value;
#ifdef XNU_MARBLE_AUDIO_MUTED_ANALOG
#include "marble_aw2032_analog_fifo.h"
static unsigned marble_i2c_amp_analog_reg,marble_i2c_amp_analog_readback,marble_i2c_amp_analog_authorized;
static uint16_t marble_i2c_amp_analog_value;
#endif
#endif
#endif
#ifdef XNU_MARBLE_AMP_QUIET_ONLY
#include "marble_aw2032_quiet.h"
static unsigned marble_i2c_amp_quiet_requested;
#endif
#endif
static unsigned marble_i2c_amp_gpio_writes,marble_i2c_amp_controller_writes,marble_i2c_amp_commands,marble_i2c_amp_dirty,marble_i2c_amp_data_writes;
static unsigned marble_i2c_amp_address;
static const uint64_t marble_i2c_amp_aux_addresses[]={0x88026c,0x880270,0x880614,0x88080c,0x880810,0x880814};
/* Resident Mu init derives RX watermark = depth-3 and RFR=depth-2 (16 words).
 * A zero RX watermark becomes a persistent level after a receive operation. */
static const uint32_t marble_i2c_amp_aux_values[]={1,2,0x7fc0347f,0,13,14};
static int marble_i2c_amp_read(void *ctx,uint64_t address,uint32_t *v)
{
    (void)ctx;if(!marble_i2c_fifo_read_allowed(address) || !marble_i2c_config_lease.active)return 1;
    *v=*(volatile uint32_t *)(uintptr_t)address;__asm__ volatile("dmb sy" ::: "memory");return 0;
}
static int marble_i2c_amp_store(uint64_t address,uint32_t value,const char *phase)
{
    unsigned allowed=0;
    uint64_t gpio=0xf100000ULL+(marble_i2c_amp_address==0x34?1:120)*0x1000ULL;
    if(marble_i2c_amp_address!=0x34 && marble_i2c_amp_address!=0x35)return 1;
    if(address==gpio)allowed=value==1 || value==0x201;
    else if(address==gpio+4)allowed=value==0 || value==2;
    else {
        for(unsigned i=0;i<6;i++)allowed|=address==marble_i2c_amp_aux_addresses[i];
        allowed|=marble_i2c_fifo_write_allowed(address,value,marble_i2c_amp_address);
#ifdef XNU_MARBLE_AMP_REGISTERS_ONLY
        if(marble_i2c_amp_registers_requested && str_eq(phase,"register-read"))
            allowed|=marble_aw2032_read_write_allowed(address,value,marble_i2c_amp_address,marble_i2c_amp_register);
#ifdef XNU_MARBLE_AMP_QUIET_ONLY
        if(marble_i2c_amp_quiet_requested && str_eq(phase,"quiet-write"))
            allowed|=marble_aw2032_quiet_write_allowed(address,value,marble_i2c_amp_address,marble_i2c_amp_sysctrl2);
        if(marble_i2c_amp_quiet_requested && str_eq(phase,"quiet-read"))
            allowed|=marble_aw2032_read_write_allowed(address,value,marble_i2c_amp_address,marble_i2c_amp_register);
#endif
#ifdef XNU_MARBLE_AMP_PROFILE_ONLY
        if(marble_i2c_amp_profile_requested && (str_eq(phase,"profile-write") || str_eq(phase,"profile-read")))
            allowed|=marble_aw2032_profile_write_allowed(address,value,marble_i2c_amp_address,marble_i2c_amp_profile_pair,marble_i2c_amp_profile_readback);
#ifdef XNU_MARBLE_AUDIO_DIGITAL_PLL
        if(marble_i2c_amp_profile_requested && (str_eq(phase,"digital-write") || str_eq(phase,"digital-read")))
            allowed|=marble_aw2032_digital_fifo_allowed(address,value,marble_i2c_amp_address,marble_i2c_amp_digital_reg,marble_i2c_amp_digital_value,marble_i2c_amp_digital_readback);
#ifdef XNU_MARBLE_AUDIO_MUTED_ANALOG
        if(marble_i2c_amp_analog_authorized && (str_eq(phase,"analog-write") || str_eq(phase,"analog-read")))
            allowed|=marble_aw2032_analog_fifo_allowed(address,value,marble_i2c_amp_address,marble_i2c_amp_analog_reg,marble_i2c_amp_analog_value,marble_i2c_amp_analog_readback);
#endif
#endif
#endif
#endif
    }
    if(!allowed || !marble_i2c_config_lease.active || marble_i2c_amp_dirty)return 1;
    *(volatile uint32_t *)(uintptr_t)address=value;__asm__ volatile("dsb sy" ::: "memory");
    if(address>=0xf100000)marble_i2c_amp_gpio_writes++;else marble_i2c_amp_controller_writes++;
    if(address==MIF_CMD)marble_i2c_amp_commands++;
#ifdef XNU_MARBLE_AMP_QUIET_ONLY
    if(address==MIF_TX && str_eq(phase,"quiet-write"))marble_i2c_amp_data_writes++;
#endif
#ifdef XNU_MARBLE_AMP_PROFILE_ONLY
    if(address==MIF_TX && str_eq(phase,"profile-write"))marble_i2c_amp_data_writes++;
#ifdef XNU_MARBLE_AUDIO_DIGITAL_PLL
    if(address==MIF_TX && str_eq(phase,"digital-write"))marble_i2c_amp_data_writes++;
#ifdef XNU_MARBLE_AUDIO_MUTED_ANALOG
    if(address==MIF_TX && str_eq(phase,"analog-write"))marble_i2c_amp_data_writes++;
#endif
#endif
#endif
    print("marble-i2c-amp: write address=%x value=%x phase=%s device=%x\n",address,(uint64_t)value,phase,(uint64_t)marble_i2c_amp_address);
    return 0;
}
static int marble_i2c_amp_write(void *ctx,uint64_t a,uint32_t v)
{
    (void)ctx;if(!marble_i2c_fifo_write_allowed(a,v,marble_i2c_amp_address))return 1;
    return marble_i2c_amp_store(a,v,"transfer");
}
static uint64_t marble_i2c_amp_now(void *ctx)
{
    (void)ctx;uint64_t ticks=READ_SYSREG(CNTVCT_EL0),hz=READ_SYSREG(CNTFRQ_EL0);
    return ticks/hz*1000000+(ticks%hz)*1000000/hz;
}
static void marble_i2c_amp_delay(void *ctx,unsigned us)
{(void)ctx;BS->stall(us);}
#ifdef XNU_MARBLE_AMP_REGISTERS_ONLY
static int marble_i2c_amp_register_write(void *ctx,uint64_t a,uint32_t v)
{
    (void)ctx;
    if(!marble_aw2032_read_write_allowed(a,v,marble_i2c_amp_address,marble_i2c_amp_register))return 1;
    return marble_i2c_amp_store(a,v,"register-read");
}
static int marble_i2c_amp_snapshot(unsigned address)
{
    MarbleI2cFifoOps o={NULL,marble_i2c_amp_read,marble_i2c_amp_register_write,marble_i2c_amp_now,marble_i2c_amp_delay};
    uint16_t sysctrl=0,sysctrl2=0;unsigned count=0;int ok=1;
    for(unsigned i=0;i<MARBLE_AW2032_SNAPSHOT_WORDS;i++) {
        marble_i2c_amp_register=marble_aw2032_snapshot_registers[i];MarbleI2cFifoResult r={0};
        ok=marble_aw2032_read_register(&o,address,marble_i2c_amp_register,&r);
        print("marble-aw-reg: read device=%x index=%d reg=%x PASS=%d value=%x commands=%d tx_words=%d rx_words=%d aborts=%d dirty=%d elapsed_us=%d\n",
            (uint64_t)address,(uint64_t)i,(uint64_t)marble_i2c_amp_register,(uint64_t)ok,(uint64_t)r.chip_id,
            (uint64_t)r.commands,(uint64_t)r.tx_words,(uint64_t)r.rx_words,(uint64_t)r.aborts,(uint64_t)r.dirty,r.elapsed_us);
        if(r.dirty)marble_i2c_amp_register_dirty=1;
        if(!ok)break;
        if(marble_i2c_amp_register==4)sysctrl=r.chip_id;
        if(marble_i2c_amp_register==5)sysctrl2=r.chip_id;
#ifdef XNU_MARBLE_AMP_PROFILE_ONLY
        if(marble_i2c_amp_register==0x78)marble_i2c_amp_efrh=r.chip_id;
        if(marble_i2c_amp_register==0x7a)marble_i2c_amp_efrm1=r.chip_id;
#endif
        count++;
    }
    unsigned powered_down=(sysctrl&3)==3,muted=!!(sysctrl2&0x10);
    marble_i2c_amp_sysctrl=sysctrl;marble_i2c_amp_sysctrl2=sysctrl2;
    ok=ok && count==MARBLE_AW2032_SNAPSHOT_WORDS && powered_down && muted;
    print("marble-aw-reg: complete device=%x PASS=%d reads=%d powered_down=%d muted=%d amplifier_register_writes=0 speaker_switch_writes=0 audio_acceptance=0\n",
        (uint64_t)address,(uint64_t)ok,(uint64_t)count,(uint64_t)powered_down,(uint64_t)muted);
    return ok;
}
#ifdef XNU_MARBLE_AMP_QUIET_ONLY
static int marble_i2c_amp_quiet_write_callback(void *ctx,uint64_t a,uint32_t v)
{
    (void)ctx;
    if(!marble_aw2032_quiet_write_allowed(a,v,marble_i2c_amp_address,marble_i2c_amp_sysctrl2))return 1;
    return marble_i2c_amp_store(a,v,"quiet-write");
}
static int marble_i2c_amp_quiet_read_callback(void *ctx,uint64_t a,uint32_t v)
{
    (void)ctx;
    if(!marble_aw2032_read_write_allowed(a,v,marble_i2c_amp_address,marble_i2c_amp_register))return 1;
    return marble_i2c_amp_store(a,v,"quiet-read");
}
static int marble_i2c_amp_quiet(unsigned address)
{
    unsigned writes_before=marble_i2c_amp_data_writes;
    MarbleI2cFifoOps o={NULL,marble_i2c_amp_read,marble_i2c_amp_quiet_write_callback,marble_i2c_amp_now,marble_i2c_amp_delay};
    MarbleI2cFifoResult w={0};uint16_t after[2]={0};unsigned count=0;
    int ok=marble_aw2032_quiet_write(&o,address,marble_i2c_amp_sysctrl,marble_i2c_amp_sysctrl2,&w);
    if(w.dirty)marble_i2c_amp_register_dirty=1;
    o.write=marble_i2c_amp_quiet_read_callback;
    for(unsigned i=0;ok && i<2;i++) {
        marble_i2c_amp_register=4+i;MarbleI2cFifoResult r={0};
        ok=marble_aw2032_read_register(&o,address,4+i,&r);after[i]=r.chip_id;
        print("marble-aw-quiet: read device=%x index=%d reg=%x PASS=%d value=%x commands=%d tx_words=%d rx_words=%d aborts=%d dirty=%d elapsed_us=%d\n",
            (uint64_t)address,(uint64_t)i,(uint64_t)(4+i),(uint64_t)ok,(uint64_t)r.chip_id,
            (uint64_t)r.commands,(uint64_t)r.tx_words,(uint64_t)r.rx_words,(uint64_t)r.aborts,(uint64_t)r.dirty,r.elapsed_us);
        if(r.dirty)marble_i2c_amp_register_dirty=1;
        if(ok)count++;
    }
    ok=ok && count==2 && after[0]==marble_i2c_amp_sysctrl && after[1]==marble_aw2032_quiet_value(marble_i2c_amp_sysctrl2);
    print("marble-aw-quiet: complete device=%x PASS=%d before=%x target=%x after=%x sysctrl=%x readbacks=%d commands=%d tx_words=%d rx_words=%d aborts=%d dirty=%d elapsed_us=%d amplifier_register_writes=%d speaker_switch_writes=0 audio_acceptance=0\n",
        (uint64_t)address,(uint64_t)ok,(uint64_t)marble_i2c_amp_sysctrl2,(uint64_t)marble_aw2032_quiet_value(marble_i2c_amp_sysctrl2),
        (uint64_t)after[1],(uint64_t)after[0],(uint64_t)count,(uint64_t)w.commands,(uint64_t)w.tx_words,(uint64_t)w.rx_words,
        (uint64_t)w.aborts,(uint64_t)w.dirty,w.elapsed_us,(uint64_t)(marble_i2c_amp_data_writes-writes_before));
    return ok;
}
#endif
#endif
#ifdef XNU_MARBLE_AMP_PROFILE_ONLY
static int marble_i2c_amp_profile_callback(void *ctx,uint64_t a,uint32_t v)
{
    (void)ctx;
    if(!marble_aw2032_profile_write_allowed(a,v,marble_i2c_amp_address,marble_i2c_amp_profile_pair,marble_i2c_amp_profile_readback))return 1;
    return marble_i2c_amp_store(a,v,marble_i2c_amp_profile_readback?"profile-read":"profile-write");
}
static int marble_i2c_amp_profile(unsigned address)
{
    MarbleI2cFifoOps o={NULL,marble_i2c_amp_read,marble_i2c_amp_profile_callback,marble_i2c_amp_now,marble_i2c_amp_delay};
    unsigned count=0,writes_before=marble_i2c_amp_data_writes;int ok=1;uint16_t calibration=0;
    if(!marble_aw2032_vcalb(marble_i2c_amp_efrh,marble_i2c_amp_efrm1,&calibration))return 0;
    for(unsigned i=0;i<MARBLE_AW2032_PROFILE_WORDS;i++) {
        MarbleI2cFifoResult w={0},r={0};
        ok=marble_aw2032_profile_pair(address,i,marble_i2c_amp_sysctrl,marble_i2c_amp_sysctrl2,marble_i2c_amp_efrh,marble_i2c_amp_efrm1,&marble_i2c_amp_profile_pair);
        marble_i2c_amp_profile_readback=0;
        if(ok)ok=marble_aw2032_profile_write(&o,address,marble_i2c_amp_profile_pair,&w);
        marble_i2c_amp_profile_readback=1;
        if(ok)ok=marble_aw2032_read_register_raw(&o,address,marble_i2c_amp_profile_pair.reg,&r);
        if(w.dirty || r.dirty)marble_i2c_amp_register_dirty=1;
        ok=ok && r.chip_id==marble_i2c_amp_profile_pair.value;
        print("marble-aw-profile: word device=%x index=%d reg=%x PASS=%d target=%x observed=%x write_commands=%d read_commands=%d tx_words=%d rx_words=%d aborts=%d dirty=%d elapsed_us=%d\n",
            (uint64_t)address,(uint64_t)i,(uint64_t)marble_i2c_amp_profile_pair.reg,(uint64_t)ok,(uint64_t)marble_i2c_amp_profile_pair.value,
            (uint64_t)r.chip_id,(uint64_t)w.commands,(uint64_t)r.commands,(uint64_t)(w.tx_words+r.tx_words),(uint64_t)r.rx_words,
            (uint64_t)(w.aborts+r.aborts),(uint64_t)(w.dirty|r.dirty),w.elapsed_us+r.elapsed_us);
        if(!ok)break;count++;
    }
    ok=ok && count==MARBLE_AW2032_PROFILE_WORDS;
    print("marble-aw-profile: complete device=%x PASS=%d words=%d efrh=%x efrm1=%x vcalb=%x amplifier_register_writes=%d power_up=0 speaker_switch_writes=0 audio_acceptance=0\n",
        (uint64_t)address,(uint64_t)ok,(uint64_t)count,(uint64_t)marble_i2c_amp_efrh,(uint64_t)marble_i2c_amp_efrm1,(uint64_t)calibration,
        (uint64_t)(marble_i2c_amp_data_writes-writes_before));
    return ok;
}
#endif
#ifdef XNU_MARBLE_AUDIO_DIGITAL_PLL
static int marble_i2c_amp_digital_fifo(void *ctx,uint64_t a,uint32_t v)
{
    (void)ctx;
    if(!marble_aw2032_digital_fifo_allowed(a,v,marble_i2c_amp_address,marble_i2c_amp_digital_reg,marble_i2c_amp_digital_value,marble_i2c_amp_digital_readback))return 1;
    return marble_i2c_amp_store(a,v,marble_i2c_amp_digital_readback?"digital-read":"digital-write");
}
static int marble_i2c_amp_digital_read(void *ctx,unsigned address,unsigned reg,uint16_t *v)
{
    (void)ctx;if(address!=marble_i2c_amp_address || (reg!=1 && reg!=4 && reg!=5) || !v)return 1;
    marble_i2c_amp_digital_reg=reg;marble_i2c_amp_digital_readback=1;
    MarbleI2cFifoOps o={NULL,marble_i2c_amp_read,marble_i2c_amp_digital_fifo,marble_i2c_amp_now,marble_i2c_amp_delay};MarbleI2cFifoResult r={0};
    int ok=marble_aw2032_read_register_raw(&o,address,reg,&r);*v=r.chip_id;
    if(r.dirty)marble_i2c_amp_register_dirty=1;
    print("marble-aw-digital: access device=%x read=1 reg=%x value=%x PASS=%d commands=%d dirty=%d\n",(uint64_t)address,(uint64_t)reg,(uint64_t)*v,(uint64_t)ok,(uint64_t)r.commands,(uint64_t)r.dirty);
    return !ok;
}
static int marble_i2c_amp_digital_write(void *ctx,unsigned address,unsigned reg,uint16_t value)
{
    (void)ctx;if(address!=marble_i2c_amp_address || !marble_aw2032_digital_allowed(address,reg,value))return 1;
    marble_i2c_amp_digital_reg=reg;marble_i2c_amp_digital_value=value;marble_i2c_amp_digital_readback=0;
    MarbleI2cFifoOps o={NULL,marble_i2c_amp_read,marble_i2c_amp_digital_fifo,marble_i2c_amp_now,marble_i2c_amp_delay};MarbleI2cFifoResult r={0};
    int ok=marble_aw2032_digital_write(&o,address,reg,value,&r);
    if(r.dirty)marble_i2c_amp_register_dirty=1;
    print("marble-aw-digital: access device=%x read=0 reg=%x value=%x PASS=%d commands=%d dirty=%d\n",(uint64_t)address,(uint64_t)reg,(uint64_t)value,(uint64_t)ok,(uint64_t)r.commands,(uint64_t)r.dirty);
    return !ok;
}
#ifdef XNU_MARBLE_AUDIO_MUTED_ANALOG
static int marble_i2c_amp_analog_fifo(void *ctx,uint64_t a,uint32_t v)
{
    (void)ctx;
    if(!marble_i2c_amp_analog_authorized)return 1;
    if(!marble_aw2032_analog_fifo_allowed(a,v,marble_i2c_amp_address,marble_i2c_amp_analog_reg,marble_i2c_amp_analog_value,marble_i2c_amp_analog_readback))return 1;
    return marble_i2c_amp_store(a,v,marble_i2c_amp_analog_readback?"analog-read":"analog-write");
}
static int marble_i2c_amp_analog_read(void *ctx,unsigned address,unsigned reg,uint16_t *v)
{
    (void)ctx;if(!marble_i2c_amp_analog_authorized || address!=marble_i2c_amp_address || (reg!=1 && reg!=4 && reg!=5) || !v)return 1;
    marble_i2c_amp_analog_reg=reg;marble_i2c_amp_analog_readback=1;
    MarbleI2cFifoOps o={NULL,marble_i2c_amp_read,marble_i2c_amp_analog_fifo,marble_i2c_amp_now,marble_i2c_amp_delay};MarbleI2cFifoResult r={0};
    int ok=marble_aw2032_read_register_raw(&o,address,reg,&r);*v=r.chip_id;
    if(r.dirty)marble_i2c_amp_register_dirty=1;
    print("marble-aw-analog: access device=%x read=1 reg=%x value=%x PASS=%d commands=%d dirty=%d\n",(uint64_t)address,(uint64_t)reg,(uint64_t)*v,(uint64_t)ok,(uint64_t)r.commands,(uint64_t)r.dirty);
    return !ok;
}
static int marble_i2c_amp_analog_write(void *ctx,unsigned address,unsigned reg,uint16_t value)
{
    (void)ctx;if(!marble_i2c_amp_analog_authorized || address!=marble_i2c_amp_address || !marble_aw2032_analog_allowed(address,reg,value))return 1;
    marble_i2c_amp_analog_reg=reg;marble_i2c_amp_analog_value=value;marble_i2c_amp_analog_readback=0;
    MarbleI2cFifoOps o={NULL,marble_i2c_amp_read,marble_i2c_amp_analog_fifo,marble_i2c_amp_now,marble_i2c_amp_delay};MarbleI2cFifoResult r={0};
    int ok=marble_aw2032_analog_write(&o,address,reg,value,&r);
    if(r.dirty)marble_i2c_amp_register_dirty=1;
    print("marble-aw-analog: access device=%x read=0 reg=%x value=%x PASS=%d commands=%d dirty=%d\n",(uint64_t)address,(uint64_t)reg,(uint64_t)value,(uint64_t)ok,(uint64_t)r.commands,(uint64_t)r.dirty);
    return !ok;
}
static int marble_i2c_amp_muted_analog(void *ctx,unsigned address)
{
    (void)ctx;
    if(!marble_i2c_amp_digital_authorized || !marble_pil_auth_attempted || !marble_qup_keepalive_held ||
       marble_i2c_amp_analog_authorized || address!=marble_i2c_amp_address)return 0;
    marble_i2c_amp_analog_authorized=1;
    MarbleAwDigitalOps o={NULL,marble_i2c_amp_analog_read,marble_i2c_amp_analog_write,marble_i2c_amp_delay};
    MarbleAwAnalogResult r={0};uint64_t began=marble_i2c_amp_now(NULL);
    int ok=marble_aw2032_analog_cycle(&o,address,&r);
    marble_i2c_amp_analog_authorized=0;
    if(r.dirty)marble_i2c_amp_register_dirty=1;
    print("marble-aw-analog: complete device=%x PASS=%d attempted=%d writes=%d polls=%d ready=%d restored=%d dirty=%d status=%x HMUTE_retained=1 attenuation_db=90 speaker_switch_writes=0 audio_acceptance=0 elapsed_us=%d\n",
        (uint64_t)address,(uint64_t)ok,(uint64_t)r.attempted,(uint64_t)r.writes,(uint64_t)r.polls,(uint64_t)r.ready,
        (uint64_t)r.restored,(uint64_t)r.dirty,(uint64_t)r.status,marble_i2c_amp_now(NULL)-began);
    return ok;
}
#endif
static int marble_i2c_amp_digital(unsigned address)
{
    if(!marble_i2c_amp_digital_authorized || !marble_pil_auth_attempted || !marble_qup_keepalive_held)return 0;
    MarbleAwDigitalOps o={NULL,marble_i2c_amp_digital_read,marble_i2c_amp_digital_write,marble_i2c_amp_delay};MarbleAwDigitalResult r={0};
#ifdef XNU_MARBLE_AUDIO_MUTED_ANALOG
    o.locked_probe=marble_i2c_amp_muted_analog;
    const unsigned amppd_retained=0;
#else
    const unsigned amppd_retained=1;
#endif
    int ok=marble_aw2032_digital_cycle(&o,address,&r);
    if(r.dirty)marble_i2c_amp_register_dirty=1;
    print("marble-aw-digital: complete device=%x PASS=%d attempted=%d writes=%d polls=%d locked=%d restored=%d dirty=%d sysctrl=%x sysctrl2=%x status=%x AMPPD_retained=%d HMUTE_retained=1 attenuation_db=90 speaker_switch_writes=0 audio_acceptance=0\n",
        (uint64_t)address,(uint64_t)ok,(uint64_t)r.attempted,(uint64_t)r.writes,(uint64_t)r.polls,(uint64_t)r.locked,(uint64_t)r.restored,(uint64_t)r.dirty,(uint64_t)r.sysctrl,(uint64_t)r.sysctrl2,(uint64_t)r.status,(uint64_t)amppd_retained);
    if(marble_pil_progress)marble_pil_progress("aud-PLL",((uint64_t)address<<32)|((uint64_t)ok<<16)|r.status);
    return ok;
}
#endif
#ifdef XNU_MARBLE_AUDIO_BOARD_HELD
#include "marble_i2c_board_held_fw.h"
#endif
static int marble_i2c_amp_one(unsigned address)
{
#ifdef XNU_MARBLE_AUDIO_AMP_PROFILE
    if(marble_pil_progress)marble_pil_progress("audio-amp-one-begin",address);
#endif
    unsigned data_writes_before=marble_i2c_amp_data_writes;
    marble_i2c_amp_address=address;
    const uint64_t gpio=0xf100000ULL+(address==0x34?1:120)*0x1000ULL;
    uint64_t targets[8]={gpio,gpio+4,0x88026c,0x880270,0x880614,0x88080c,0x880810,0x880814};
    for(unsigned i=0;i<8;i++) {
        uint64_t a=targets[i],par=marble_fw_translation(a);
        if(par&1 || (par&0x0000fffffffff000ULL)!=(a&~4095ULL))return 0;
    }
    uint32_t cfg=*(volatile uint32_t *)(uintptr_t)gpio,io=*(volatile uint32_t *)(uintptr_t)(gpio+4),saved[6];
    if(cfg!=1 || (io&3))return 0;
    MarbleI2cFifoOps o={NULL,marble_i2c_amp_read,marble_i2c_amp_write,marble_i2c_amp_now,marble_i2c_amp_delay};
    if(!marble_i2c_fifo_clean(&o))return 0;
    for(unsigned i=0;i<6;i++) {
        saved[i]=*(volatile uint32_t *)(uintptr_t)marble_i2c_amp_aux_addresses[i];
        print("marble-i2c-amp: saved device=%x index=%d address=%x value=%x desired=%x\n",(uint64_t)address,(uint64_t)i,marble_i2c_amp_aux_addresses[i],(uint64_t)saved[i],(uint64_t)marble_i2c_amp_aux_values[i]);
    }
    print("marble-i2c-amp: reset-saved device=%x gpio=%x cfg=%x io=%x\n",(uint64_t)address,gpio,(uint64_t)cfg,(uint64_t)io);
    unsigned aux_changed=0,reset_touched=0;int ok=1;MarbleI2cFifoResult r={0};
    for(unsigned i=0;i<6;i++)if(saved[i]!=marble_i2c_amp_aux_values[i]) {
        aux_changed|=1u<<i;
        if(marble_i2c_amp_store(marble_i2c_amp_aux_addresses[i],marble_i2c_amp_aux_values[i],"prepare") ||
           *(volatile uint32_t *)(uintptr_t)marble_i2c_amp_aux_addresses[i]!=marble_i2c_amp_aux_values[i]){ok=0;break;}
    }
    if(ok) {
        reset_touched=1;
        ok=!marble_i2c_amp_store(gpio,0x201,"reset-output") &&
            *(volatile uint32_t *)(uintptr_t)gpio==0x201 && !marble_i2c_amp_store(gpio+4,2,"reset-release") &&
            (*(volatile uint32_t *)(uintptr_t)(gpio+4)&2)==2;
        BS->stall(2000);
        if(ok)ok=marble_i2c_fifo_id(&o,address,&r);
        print("marble-i2c-amp: transfer device=%x PASS=%d attempted=%d commands=%d tx_words=%d rx_words=%d raw_rx=%x chip_id=%x polls=%d aborts=%d dirty=%d elapsed_us=%d IRQ_seen=%x IRQ_last=%x S_IRQ_seen=%x S_IRQ_last=%x idle_IRQ=%x idle_IRQ_enable=%x\n",
            (uint64_t)address,(uint64_t)ok,(uint64_t)r.attempted,(uint64_t)r.commands,(uint64_t)r.tx_words,(uint64_t)r.rx_words,(uint64_t)r.raw_rx,(uint64_t)r.chip_id,(uint64_t)r.polls,(uint64_t)r.aborts,(uint64_t)r.dirty,r.elapsed_us,(uint64_t)r.observed_irq,(uint64_t)r.last_irq,(uint64_t)r.observed_s_irq,(uint64_t)r.last_s_irq,(uint64_t)r.idle_irq,(uint64_t)r.idle_irq_enable);
    }
    int registers_ok=1;
#ifdef XNU_MARBLE_AMP_REGISTERS_ONLY
    if(marble_i2c_amp_registers_requested)registers_ok=ok && r.chip_id==0x2032 && marble_i2c_amp_snapshot(address);
#ifdef XNU_MARBLE_AMP_QUIET_ONLY
    if(marble_i2c_amp_quiet_requested)registers_ok=registers_ok && marble_i2c_amp_quiet(address);
#endif
#ifdef XNU_MARBLE_AMP_PROFILE_ONLY
    if(marble_i2c_amp_profile_requested)registers_ok=registers_ok && marble_i2c_amp_profile(address);
#ifdef XNU_MARBLE_AUDIO_DIGITAL_PLL
    if(marble_i2c_amp_profile_requested) {
#ifdef XNU_MARBLE_AUDIO_BOARD_HELD
        if(marble_i2c_board_held_requested)registers_ok=registers_ok && marble_i2c_board_begin(address);
        else
#endif
        registers_ok=registers_ok && marble_i2c_amp_digital(address);
    }
#endif
#endif
#endif
#ifdef XNU_MARBLE_AUDIO_BOARD_HELD
    if(marble_i2c_board_held_requested) {
        unsigned index=address==0x34?0:1;
        marble_i2c_board_device[index].prepared=1;marble_i2c_board_device[index].cfg=cfg;
        marble_i2c_board_device[index].reset_touched=reset_touched;
        marble_i2c_board_device[index].aux_changed=aux_changed;
        marble_i2c_board_device[index].bus_dirty=r.dirty;
        memcpy(marble_i2c_board_device[index].saved,saved,sizeof(saved));
        return ok && registers_ok && r.chip_id==0x2032 && !r.dirty && !marble_i2c_amp_register_dirty;
    }
#endif
    /* Always assert reset before config/clock release, including a stuck bus.
     * Never enable the speaker switch or write amplifier register data. */
    unsigned reset_restored=1;
    if(reset_touched) {
        reset_restored=!marble_i2c_amp_store(gpio+4,0,"reset-assert");BS->stall(1000);
        reset_restored=reset_restored && !marble_i2c_amp_store(gpio,cfg,"reset-restore") &&
            *(volatile uint32_t *)(uintptr_t)gpio==cfg && !(*(volatile uint32_t *)(uintptr_t)(gpio+4)&2);
    }
    unsigned bus_clean=!r.dirty && marble_i2c_fifo_clean(&o),aux_restored=1;
#ifdef XNU_MARBLE_AMP_REGISTERS_ONLY
    bus_clean=bus_clean && !marble_i2c_amp_register_dirty;
#endif
    if(bus_clean) {
        for(int i=5;i>=0;i--)if(aux_changed&(1u<<i)) {
            if(marble_i2c_amp_store(marble_i2c_amp_aux_addresses[i],saved[i],"restore") ||
               *(volatile uint32_t *)(uintptr_t)marble_i2c_amp_aux_addresses[i]!=saved[i])aux_restored=0;
        }
    } else aux_restored=0;
    bus_clean=bus_clean && aux_restored && marble_i2c_fifo_clean(&o);
    uint32_t post_irq=*(volatile uint32_t *)(uintptr_t)MIF_IRQ,post_enable=*(volatile uint32_t *)(uintptr_t)MIF_IRQ_ENABLE;
    marble_i2c_amp_dirty=!reset_restored || !bus_clean || !aux_restored;
    unsigned identity=ok && registers_ok && r.chip_id==0x2032 && !marble_i2c_amp_dirty;
    print("marble-i2c-amp: complete device=%x PASS=%d chip_id=%x reset_restored=%d bus_clean=%d aux_restored=%d dirty=%d logical_quiesce=%d raw_IRQ_restored=%d post_IRQ=%x post_IRQ_enable=%x cold_reset_required=1 amplifier_register_writes=%d speaker_switch_writes=0 audio_acceptance=0\n",
        (uint64_t)address,(uint64_t)identity,(uint64_t)r.chip_id,(uint64_t)reset_restored,(uint64_t)bus_clean,(uint64_t)aux_restored,(uint64_t)marble_i2c_amp_dirty,(uint64_t)bus_clean,(uint64_t)!post_irq,(uint64_t)post_irq,(uint64_t)post_enable,(uint64_t)(marble_i2c_amp_data_writes-data_writes_before));
#ifdef XNU_MARBLE_AUDIO_AMP_PROFILE
    if(marble_pil_progress)marble_pil_progress("audio-amp-one-end",((uint64_t)address<<32)|((uint64_t)identity<<24)|marble_i2c_amp_commands);
#endif
    return identity;
}
static int marble_i2c_amp_id(void)
{
    if(!marble_i2c_config_lease.active || !marble_i2c_clock_lease.active || marble_i2c_amp_dirty)return 0;
    return marble_i2c_amp_one(0x34) && marble_i2c_amp_one(0x35);
}
#endif
