/* Firmware discovery without display/clock MMIO. ABI references and licenses:
 * out/vsync-v1-plan/mu-display-reference.json (pinned Mu-Silicium commit). */
#ifndef XNULOADER_MARBLE_DISPLAY_FW_H
#define XNULOADER_MARBLE_DISPLAY_FW_H
typedef struct marble_fw_clock {
    uint64_t revision;
    efi_status (EFIAPI *get_id)(struct marble_fw_clock *,const char *,uint64_t *);
    efi_status (EFIAPI *enable)(struct marble_fw_clock *,uint64_t);
    efi_status (EFIAPI *disable)(struct marble_fw_clock *,uint64_t);
    efi_status (EFIAPI *is_enabled)(struct marble_fw_clock *,uint64_t,uint8_t *);
    efi_status (EFIAPI *is_on)(struct marble_fw_clock *,uint64_t,uint8_t *);
    void *set_frequency;
    efi_status (EFIAPI *get_frequency)(struct marble_fw_clock *,uint64_t,uint32_t *);
} marble_fw_clock;
typedef struct marble_fw_display_power {
    uint32_t revision;
    efi_status (EFIAPI *set)(struct marble_fw_display_power *,uint32_t);
    efi_status (EFIAPI *get)(struct marble_fw_display_power *,uint32_t *);
} marble_fw_display_power;
#include "marble_display_clock.h"
#include "marble_dsi.h"
static MarbleDisplayClock marble_display_bus;
static marble_fw_clock *marble_display_clock_owner;
static unsigned marble_display_qualified;
static MarbleBrightnessLease marble_brightness;
static uint64_t marble_display_base;
static int marble_fw_clock_id(void *ctx,const char *name,uint64_t *id)
{marble_fw_clock *c=ctx;return EFI_ERROR(c->get_id(c,name,id));}
static int marble_fw_clock_state(void *ctx,uint64_t id,unsigned *enabled,unsigned *on)
{
    marble_fw_clock *c=ctx;uint8_t e=255,n=255;
    efi_status a=c->is_enabled(c,id,&e),b=c->is_on(c,id,&n);
    print("marble-display-bus: state id=%x enabled=%d on=%d status=%x,%x\n",id,(uint64_t)e,(uint64_t)n,a,b);
    *enabled=e;*on=n;return EFI_ERROR(a) || EFI_ERROR(b);
}
static int marble_fw_clock_enable(void *ctx,uint64_t id)
{marble_fw_clock *c=ctx;efi_status s=c->enable(c,id);print("marble-display-bus: enable id=%x status=%x\n",id,s);return EFI_ERROR(s);}
static int marble_fw_clock_disable(void *ctx,uint64_t id)
{marble_fw_clock *c=ctx;efi_status s=c->disable(c,id);print("marble-display-bus: disable id=%x status=%x\n",id,s);return EFI_ERROR(s);}
static MarbleClockOps marble_display_clock_ops(marble_fw_clock *c)
{
    MarbleClockOps o={c,marble_fw_clock_id,marble_fw_clock_state,
        marble_fw_clock_enable,marble_fw_clock_disable};return o;
}

/* AT walks translation tables without loading from the target. */
static uint64_t marble_fw_translation(uint64_t address)
{
    uint64_t saved=READ_SYSREG(PAR_EL1),result;
    __asm__ volatile("at s1e1r, %1\nisb\nmrs %0, par_el1" : "=r"(result) : "r"(address) : "memory");
    __asm__ volatile("msr par_el1, %0\nisb" :: "r"(saved) : "memory");
    return result;
}
static void marble_display_info(unsigned clock_status)
{
    static const efi_guid clock_guid={0x241afae6,0x885f,0x4f6c,{0xa7,0xea,0xc2,0x8e,0xab,0x79,0xc3,0xe5}};
    static const efi_guid power_guid={0xf352021d,0x9593,0x4432,{0xbf,4,0x67,0xb9,0xf3,0xb7,0x60,8}};
    static const efi_guid utils_guid={0xc0dd69ac,0x76ba,0x11e6,{0xab,0x24,0x1f,0xc7,0xf5,0x57,0x5f,0x19}};
    static const efi_guid gop_guid=EFI_GOP_GUID;
    static const uint64_t addresses[]={0xae00000,0xae36000,0xae37000,0xaf00000,0xaf080a4,0x100000,0x17d92320};
    print("marble-display-fw: discovery-v1 target_reads=0 clock_writes=0 el=%d\n",READ_SYSREG(CurrentEL)>>2);
    for(unsigned i=0;i<sizeof(addresses)/sizeof(addresses[0]);i++) {
        uint64_t par=marble_fw_translation(addresses[i]);
        print("marble-display-fw: translation va=%x par=%x mapped=%d\n",addresses[i],par,(uint64_t)!(par&1));
    }
    marble_fw_clock *clock=NULL;
    marble_fw_display_power *power=NULL;
    void *utils=NULL;
    efi_gop *gop=NULL;
    efi_status c=BS->locate_protocol(&clock_guid,NULL,(void **)&clock);
    efi_status p=BS->locate_protocol(&power_guid,NULL,(void **)&power);
    efi_status u=BS->locate_protocol(&utils_guid,NULL,&utils);
    efi_status g=BS->locate_protocol(&gop_guid,NULL,(void **)&gop);
    print("marble-display-fw: protocols clock=%x power=%x utils=%x gop=%x\n",c,p,u,g);
    if(!EFI_ERROR(g) && gop && gop->mode && gop->mode->info)
        print("marble-display-fw: GOP mode=%d count=%d width=%d height=%d stride=%d base=%x bytes=%d\n",
            (uint64_t)gop->mode->mode,(uint64_t)gop->mode->max_mode,
            (uint64_t)gop->mode->info->width,(uint64_t)gop->mode->info->height,
            (uint64_t)gop->mode->info->pixels_per_scanline,gop->mode->fb_base,gop->mode->fb_size);
    /* GetClockID is a name lookup; do not call hardware IsOn/IsEnabled. */
    if(!EFI_ERROR(c) && clock && clock->get_id) {
        static const char *names[]={"gcc_disp_ahb_clk","gcc_disp_hf_axi_clk","gcc_disp_sf_axi_clk",
            "disp_cc_mdss_ahb_clk","disp_cc_mdss_non_gdsc_ahb_clk","disp_cc_mdss_mdp_clk",
            "disp_cc_mdss_vsync_clk","disp_cc_mdss_byte0_clk","disp_cc_mdss_pclk0_clk"};
        print("marble-display-fw: clock-revision=%x\n",clock->revision);
        for(unsigned i=0;i<sizeof(names)/sizeof(names[0]);i++) {
            uint64_t id=0;efi_status s=clock->get_id(clock,names[i],&id);
            print("marble-display-fw: clock-name=%s status=%x id=%x\n",names[i],s,id);
            if(clock_status && !EFI_ERROR(s) && clock->is_enabled && clock->is_on && clock->get_frequency) {
                uint8_t enabled=255,on=255;uint32_t hz=0;
                efi_status a=clock->is_enabled(clock,id,&enabled);
                efi_status b=clock->is_on(clock,id,&on);
                efi_status f=clock->get_frequency(clock,id,&hz);
                print("marble-display-fw: clock-state name=%s enabled=%d on=%d hz=%d status=%x,%x,%x\n",
                    names[i],(uint64_t)enabled,(uint64_t)on,(uint64_t)hz,a,b,f);
            }
        }
    }
    if(!EFI_ERROR(p) && power)
        print("marble-display-fw: power-revision=%x get=%x set=%x (not called)\n",
            (uint64_t)power->revision,(uint64_t)(uintptr_t)power->get,(uint64_t)(uintptr_t)power->set);
    print("marble-display-fw: discovery complete target_reads=0 clock_writes=0\n");
}
static int marble_display_clock_release(void)
{
    if(marble_brightness.active || marble_brightness.dirty) {
        marble_display_bus.dirty=1;
        print("marble-display-bus: brightness lease unresolved; clock release refused\n");return 0;
    }
    if(!marble_display_clock_owner)return 0;
    MarbleClockOps o=marble_display_clock_ops(marble_display_clock_owner);
    int restored=marble_display_clock_restore(&o,&marble_display_bus);
    marble_display_qualified=0;
    print("marble-display-bus: restored PASS=%d active=%d held=%x uncertain=%x dirty=%d\n",
        (uint64_t)restored,(uint64_t)marble_display_bus.active,(uint64_t)marble_display_bus.held,
        (uint64_t)marble_display_bus.uncertain,(uint64_t)marble_display_bus.dirty);
    return restored;
}
/* Acquire the same validated register-bus lease for both DMA audits and
 * counter qualification. Acquiring it does not qualify panel timing. */
static int marble_display_bus_acquire(void)
{
    static const efi_guid clock_guid={0x241afae6,0x885f,0x4f6c,{0xa7,0xea,0xc2,0x8e,0xab,0x79,0xc3,0xe5}};
    marble_fw_clock *clock=NULL;
    if(EFI_ERROR(BS->locate_protocol(&clock_guid,NULL,(void **)&clock)) || !clock ||
       clock->revision!=0x10009 || !clock->enable || !clock->disable ||
       !clock->is_enabled || !clock->is_on)return 0;
    /* Validate actual EL1 mappings before any register loads. */
    static const uint64_t addresses[]={0xae36000,0xae37000,0xaf00000,0x100000};
    for(unsigned i=0;i<4;i++) {
        uint64_t par=marble_fw_translation(addresses[i]);
        if((par&1) || (par&0x0000fffffffff000ULL)!=addresses[i])return 0;
    }
    MarbleClockOps o=marble_display_clock_ops(clock);
    marble_display_clock_owner=clock;marble_display_qualified=0;marble_display_base=0;
    if(!marble_display_clock_prepare(&o,&marble_display_bus))return 0;
    for(unsigned i=0;i<3;i++)
        print("marble-display-bus: before name=%s enabled=%d on=%d\n",marble_display_bus_clocks[i],
            (uint64_t)marble_display_bus.enabled[i],(uint64_t)marble_display_bus.on[i]);
    int acquired=marble_display_clock_acquire(&o,&marble_display_bus);
    print("marble-display-bus: acquired PASS=%d active=%d held=%x uncertain=%x dirty=%d\n",
        (uint64_t)acquired,(uint64_t)marble_display_bus.active,(uint64_t)marble_display_bus.held,
        (uint64_t)marble_display_bus.uncertain,(uint64_t)marble_display_bus.dirty);
    return acquired;
}
static int marble_display_counter_probe(unsigned keep)
{
    if(!marble_display_bus_acquire())return 0;
    MarbleVBlankClock counters[2]={{0},{0}};
    uint64_t frequency=READ_SYSREG(CNTFRQ_EL0);
    for(unsigned n=0;n<220;n++) {
        for(unsigned i=0;i<2;i++) {
            MarbleVBlankSample s={0};
            int valid=marble_vblank_read(marble_thermal_read,NULL,i,&s);
            int edge=marble_vblank_observe(&counters[i],&s,valid,READ_SYSREG(CNTVCT_EL0),frequency);
            if(!n || n==219 || edge)print("marble-display-counter: dsi=%d valid=%d en=%x count_en=%x frame=%d line=%d hsync=%x period=%x start=%x end=%x status=%x\n",
                (uint64_t)i,(uint64_t)valid,(uint64_t)s.enabled,(uint64_t)s.count_enabled,
                (uint64_t)s.frame,(uint64_t)s.line,(uint64_t)s.hsync,(uint64_t)s.period,
                (uint64_t)s.start,(uint64_t)s.end,(uint64_t)s.status);
        }
        BS->stall(1000);
    }
    unsigned ready=0;
    for(unsigned i=0;i<2;i++) {
        MarbleVBlankClock *c=&counters[i];
        unsigned pass=c->edges>=5 && !c->invalid && !c->missed;
        ready|=pass;
        if(pass && !marble_display_qualified) {
            marble_display_qualified=1;marble_display_base=0xae36000ULL+i*0x1000;
        }
        print("marble-display-counter: qualified dsi=%d PASS=%d edges=%d invalid=%d missed=%d min_us=%d max_us=%d\n",
            (uint64_t)i,(uint64_t)pass,(uint64_t)c->edges,(uint64_t)c->invalid,(uint64_t)c->missed,
            c->minimum_period*1000000/frequency,c->maximum_period*1000000/frequency);
    }
    if(keep && ready) {
        print("marble-display-bus: retained qualified base=%x for off-core XNU\n",marble_display_base);
        return 1;
    }
    int restored=marble_display_clock_release();
    return ready && restored;
}

/* Idle-only, read-only audit of display DMA ownership before considering the
 * ADSP carveout's overlap with Mu's Display Demura mapping. DT 20F66-marble
 * and pinned sde_hw_color_proc_v4.c/sde_hw_sspp.c define these exact offsets.
 * This never changes a fetch address, plane, demura bit or panel setting. */
#include "marble_display_dma.h"
static unsigned marble_display_memory_qualified;
static uint64_t marble_display_memory_gop,marble_display_memory_bytes;
static int marble_display_memory_info(uint64_t gop,uint64_t bytes)
{
    marble_display_memory_qualified=0;
    if(marble_display_bus.active || marble_display_bus.dirty)return 0;
    static const uint32_t pipes[]={0x5000,0x7000,0x9000,0xb000,0x25000,0x27000,0x29000,0x2b000};
    static const uint32_t demura[]={0x55000+0x15600+4,0x57000+0x14800+4};
    for(unsigned i=0;i<10;i++) {
        uint64_t pa=0xae00000ULL+(i<8?pipes[i]:demura[i-8]);pa&=~4095ULL;
        uint64_t par=marble_fw_translation(pa);
        if((par&1) || (par&0x0000fffffffff000ULL)!=pa)return 0;
    }
#if XNU_MARBLE_AUDIO_DISPLAY_DMA_ONLY
#if !XNU_MARBLE_AUDIO_OWNER_BOARD || !XNU_MARBLE_AUDIO_OWNER_STREAM
#error audio DMA-only display audit requires muted board stream24
#endif
    if(!marble_display_bus_acquire())return 0;
    print("marble-display-memory: mode=audio-DMA-only bus_lease=1 timing_qualified=0 samples=2 fields=66 interval_us=20000\n");
#else
    if(!marble_display_counter_probe(1))return 0;
#endif
    print("marble-display-memory: begin exact_DT_offsets=1 payload_access=0 register_writes=0 GOP=%x bytes=%d\n",gop,bytes);
    unsigned overlaps=0,enabled=0,stable=1;uint32_t previous[2+8*8]={0};
    for(unsigned sample=0;sample<2;sample++) {
        unsigned at=0;
        for(unsigned i=0;i<2;i++) {
            uint32_t value=*(volatile uint32_t *)(uintptr_t)(0xae00000ULL+demura[i]);
            if(value&4)enabled++;
            if(sample && previous[at]!=value)stable=0;previous[at++]=value;
            print("marble-display-memory: sample=%d dspp=%d demura_status=%x fetch_enabled=%d\n",(uint64_t)sample,(uint64_t)i,(uint64_t)value,(uint64_t)!!(value&4));
        }
        for(unsigned i=0;i<8;i++) {
            volatile uint32_t *p=(void *)(uintptr_t)(0xae00000ULL+pipes[i]);
            uint32_t size=p[0],rect1=p[0x16c/4],s0=p[0x24/4],s1=p[0x28/4];
            uint32_t h=size>>16;if((rect1>>16)>h)h=rect1>>16;
            uint32_t stride=s0&65535;
            if((s0>>16)>stride)stride=s0>>16;
            if((s1&65535)>stride)stride=s1&65535;
            if((s1>>16)>stride)stride=s1>>16;
            uint64_t extent=(uint64_t)h*stride+0x10000;
            uint32_t words[]={size,rect1,s0,s1,p[0x14/4],p[0x18/4],p[0x1c/4],p[0x20/4]};
            for(unsigned j=0;j<8;j++) {if(sample && previous[at]!=words[j])stable=0;previous[at++]=words[j];}
            unsigned hit=0;
            for(unsigned j=4;j<8;j++)if(words[j] && (uint64_t)words[j]<0xa2e00000 &&
                0x9fd00000<(uint64_t)words[j]+extent)hit=1;
            overlaps+=hit;
            print("marble-display-memory: sample=%d pipe=%d size=%x rect1=%x strides=%x,%x addresses=%x,%x,%x,%x conservative_overlap=%d\n",(uint64_t)sample,(uint64_t)i,(uint64_t)size,(uint64_t)rect1,(uint64_t)s0,(uint64_t)s1,(uint64_t)words[4],(uint64_t)words[5],(uint64_t)words[6],(uint64_t)words[7],(uint64_t)hit);
        }
        if(!sample)BS->stall(20000);
    }
    int restored=marble_display_clock_release();
    marble_display_memory_gop=gop;marble_display_memory_bytes=bytes;
    marble_display_memory_qualified=restored && stable && !enabled && !overlaps &&
        marble_display_payload_disjoint(previous,gop,bytes);
    print("marble-display-memory: exact_single_GOP_disjoint=%d scope=display_profile_only\n",(uint64_t)marble_display_memory_qualified);
    print("marble-display-memory: complete stable=%d demura_enabled_samples=%d overlapping_pipe_samples=%d bus_restored=%d audit_only=1 payload_write_approved=0\n",(uint64_t)stable,(uint64_t)enabled,(uint64_t)overlaps,(uint64_t)restored);
    return restored;
}

/* First stage of panel brightness bring-up. The primary DSI controller is
 * AP-owned at 0xae94000 in marble's DTS (v2.6). Reuse the qualified display
 * register-bus lease, then read only documented configuration/status words.
 * No DMA trigger, FIFO reset, W1C acknowledgement, BTA or panel command here. */
static int marble_dsi_info(void)
{
    const uint64_t base=0xae94000;
    static const struct { unsigned offset;const char *name; } regs[]={
        {0x000,"VERSION"},{0x004,"CTRL"},{0x008,"STATUS"},{0x00c,"FIFO_STATUS"},
        {0x010,"VIDEO_CTRL"},{0x024,"ACTIVE_H"},{0x028,"ACTIVE_V"},{0x02c,"TOTAL"},
        {0x03c,"DMA_CTRL"},{0x04c,"DMA_LENGTH"},{0x050,"DMA_FIFO_CTRL"},
        {0x068,"ACK_ERR"},{0x084,"TRIG_CTRL"},{0x0a8,"LANE_STATUS"},
        {0x0ac,"LANE_CTRL"},{0x0c0,"TIMEOUT_STATUS"},{0x100,"DMA_SCHEDULE"},
        {0x104,"DMA_WINDOW"},{0x110,"INT_CTRL"},{0x11c,"CLK_CTRL"},
        {0x120,"CLK_STATUS"},{0x15c,"TPG_CTRL"}
    };
    uint64_t par=marble_fw_translation(base);
    if((par&1) || (par&0x0000fffffffff000ULL)!=base ||
       marble_display_bus.active || marble_display_bus.dirty)return 0;
    print("marble-dsi: begin controller=%x panel_commands=0 dsi_writes=0\n",base);
    if(!marble_display_counter_probe(1))return 0;
    uint32_t version=0,control=0;
    for(unsigned i=0;i<sizeof(regs)/sizeof(regs[0]);i++) {
        uint32_t value=*(volatile uint32_t *)(uintptr_t)(base+regs[i].offset);
        if(!i)version=value;
        if(i==1)control=value;
        print("marble-dsi: offset=%x name=%s value=%x\n",
              (uint64_t)regs[i].offset,regs[i].name,(uint64_t)value);
    }
    int restored=marble_display_clock_release();
    print("marble-dsi: complete version=%x control=%x restored=%d functional_acceptance=0\n",
          (uint64_t)version,(uint64_t)control,(uint64_t)restored);
    return version && version!=~0u && (control&1) && restored;
}
static uint32_t marble_dsi_reg_read(void *ctx,unsigned offset)
{(void)ctx;return *(volatile uint32_t *)(uintptr_t)(0xae94000ULL+offset);}
static void marble_dsi_reg_write(void *ctx,unsigned offset,uint32_t value)
{
    (void)ctx;*(volatile uint32_t *)(uintptr_t)(0xae94000ULL+offset)=value;
    __asm__ volatile("dsb sy" ::: "memory");
}
static void marble_dsi_delay(void *ctx,unsigned us)
{(void)ctx;BS->stall(us);}
static int marble_dsi_wait_blank(void *ctx)
{
    (void)ctx;
    for(unsigned i=0;i<1000;i++) {
        MarbleVBlankSample s={0};
        if(!marble_vblank_read(marble_thermal_read,NULL,0,&s))return 0;
        unsigned horizontal=s.hsync>>16,total=s.period/horizontal,last=s.end/horizontal;
        if(s.line>last+8 && s.line+128<total)return 1;
        BS->stall(50);
    }
    return 0;
}
static int marble_dsi_short_probe(unsigned identity)
{
    uint64_t par=marble_fw_translation(0xae94000);
    if((par&1) || (par&0x0000fffffffff000ULL)!=0xae94000 ||
       marble_display_bus.active || marble_display_bus.dirty)return 0;
    print("marble-dsi-read: begin identity=%d panel_writes=0 max_return_changed=0\n",(uint64_t)identity);
    if(!marble_display_counter_probe(1))return 0;
    MarbleDsiOps o={NULL,marble_dsi_reg_read,marble_dsi_reg_write,marble_dsi_delay,marble_dsi_wait_blank};
    MarbleDsiResult r={0};int ok=1;
    const unsigned commands[]={0x0a,0xda,0xdb,0xdc,0x52};
    for(unsigned i=0;i<(identity?5u:1u);i++) {
    int part=i?marble_dsi_read_short(&o,&r,commands[i]):marble_dsi_read_power(&o,&r);ok=ok&&part;
    print("marble-dsi-read: command=%x PASS=%d started=%d done=%d response=%x count=%d bytes=%d data=%x,%x status=%x ints=%x ack=%x timeout=%x restored=%d dirty=%d\n",
        (uint64_t)commands[i],(uint64_t)part,
        (uint64_t)r.started,(uint64_t)r.done,(uint64_t)r.response,(uint64_t)r.read_count,(uint64_t)r.bytes,
        (uint64_t)r.data[0],(uint64_t)r.data[1],(uint64_t)r.status,(uint64_t)r.interrupts,
        (uint64_t)r.ack_error,(uint64_t)r.timeout_error,(uint64_t)r.restored,(uint64_t)r.dirty);
    if(r.dirty) {
        marble_display_bus.dirty=1;
        print("marble-dsi-read: configuration uncertain; clock lease retained; reboot required\n");
        return 0;
    }
    if(!part)break;
    }
    int restored=marble_display_clock_release();
    print("marble-dsi-read: complete PASS=%d bus_restored=%d brightness_changed=0\n",(uint64_t)ok,(uint64_t)restored);
    return ok && restored;
}
static int marble_dsi_power_probe(void) {return marble_dsi_short_probe(0);}
static int marble_dsi_identity_probe(void) {return marble_dsi_short_probe(1);}
static int marble_brightness_runtime(unsigned restore)
{
    if(!marble_display_bus.active || marble_display_bus.dirty || !marble_display_qualified)return 0;
    MarbleDsiOps o={NULL,marble_dsi_reg_read,marble_dsi_reg_write,marble_dsi_delay,marble_dsi_wait_blank};
    int ok;
    if(restore)ok=marble_brightness_restore(&o,&marble_brightness);
    else {
        ok=marble_brightness.prepared || marble_brightness_prepare(&o,&marble_brightness);
        if(ok)ok=marble_brightness_set(&o,&marble_brightness,marble_dsi_dim_target(marble_brightness.original));
    }
    if(marble_brightness.dirty)marble_display_bus.dirty=1;
    print("marble-brightness: %s PASS=%d original=%d level=%d active=%d dirty=%d changes=%d\n",
          restore?"restored":"dimmed",(uint64_t)ok,(uint64_t)marble_brightness.original,
          (uint64_t)marble_brightness.level,(uint64_t)marble_brightness.active,
          (uint64_t)marble_brightness.dirty,(uint64_t)marble_brightness.changes);
    return ok;
}
static int marble_dsi_dim_probe(void)
{
    uint64_t par=marble_fw_translation(0xae94000);
    if((par&1) || (par&0x0000fffffffff000ULL)!=0xae94000 ||
       marble_display_bus.active || marble_display_bus.dirty)return 0;
    print("marble-dsi-dim: begin bounded_ms=250 writes=brightness-only restore_required=1 max_return_changed=0\n");
    if(!marble_display_counter_probe(1))return 0;
    MarbleDsiOps o={NULL,marble_dsi_reg_read,marble_dsi_reg_write,marble_dsi_delay,marble_dsi_wait_blank};
    MarbleDsiResult r={0};unsigned original=0,target=0,attempted=0,changed=0,restored=0,ok=0;
    const unsigned commands[]={0x0a,0xda,0xdb,0xdc,0x52,0x52};
    const unsigned expected[]={0x9c,0xc2,0xb6,0x32}; /* observed on this physical marble */
    for(unsigned i=0;i<6;i++) {
        int read=marble_dsi_read_short(&o,&r,commands[i]);
        print("marble-dsi-dim: preflight command=%x PASS=%d bytes=%d data=%x,%x dirty=%d\n",
              (uint64_t)commands[i],(uint64_t)read,(uint64_t)r.bytes,
              (uint64_t)r.data[0],(uint64_t)r.data[1],(uint64_t)r.dirty);
        if(!read)goto finish;
        if(i<4) {if(r.data[0]!=expected[i])goto finish;}
        else {
            unsigned level=((unsigned)r.data[0]<<8)|r.data[1];
            if(i==4)original=level;
            else if(level!=original)goto finish;
        }
    }
    target=marble_dsi_dim_target(original);
    if(!target)goto finish;
    print("marble-dsi-dim: original=%d target=%d bl_sync_boundary_crossed=0\n",(uint64_t)original,(uint64_t)target);
    attempted=1;
    changed=marble_dsi_write_brightness(&o,&r,target);
    if(r.dirty)goto finish;
    if(changed) {
        o.delay(o.ctx,250000);
        changed=marble_dsi_read_short(&o,&r,0x52) && (((unsigned)r.data[0]<<8)|r.data[1])==target;
        print("marble-dsi-dim: lowered_readback=%d bytes=%d data=%x,%x dirty=%d\n",
              (uint64_t)changed,(uint64_t)r.bytes,(uint64_t)r.data[0],(uint64_t)r.data[1],(uint64_t)r.dirty);
        if(r.dirty)goto finish;
    }
    /* Restore even if the lowering command timed out cleanly or its readback
     * differed. Never send more commands while the engine state is dirty. */
    restored=marble_dsi_write_brightness(&o,&r,original);
    if(r.dirty)goto finish;
    if(restored)restored=marble_dsi_read_short(&o,&r,0x52) && (((unsigned)r.data[0]<<8)|r.data[1])==original;
    ok=changed && restored;
finish:
    print("marble-dsi-dim: attempted=%d lowered_verified=%d restored_verified=%d original=%d target=%d dirty=%d\n",
          (uint64_t)attempted,(uint64_t)changed,(uint64_t)restored,(uint64_t)original,(uint64_t)target,(uint64_t)r.dirty);
    if(r.dirty || (attempted && !restored)) {
        marble_display_bus.dirty=1;
        print("marble-dsi-dim: restore uncertain; lease retained; reboot required\n");return 0;
    }
    int bus=marble_display_clock_release();
    print("marble-dsi-dim: complete PASS=%d bus_restored=%d panel_level_restored=%d\n",(uint64_t)ok,(uint64_t)bus,(uint64_t)(!attempted || restored));
    return ok && bus;
}
#endif
