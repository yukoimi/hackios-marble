/* Read-only DSI video frame counters, opted in by fb_hw_vsync=1.
 * No display clock, panel, interrupt, framebuffer address or power writes.
 * This is hardware-derived polling, not an interrupt-driven page flip.
 * Timestamps are observations, with a polling uncertainty, not optical time.
 */
extern "C" vm_offset_t ml_io_map(vm_offset_t, vm_size_t);
#include "MarbleDisplayPhase.h"
static TUNABLE(uint32_t, gHardwareVSync, "fb_hw_vsync", 0);
static const char marble_display_abi[] = "MDISPLAY-MARBLE-COUNTER-v2";
struct MarbleDisplayTiming {
    volatile uint32_t * registers;
    /* The mapping lives until all XNU cores stop and EFI releases its bus
     * clocks. Runtime failure revokes eligibility, never the shared pointer:
     * another CPU may already be between enabled() and reg(). */
    uint32_t qualified;
    uint64_t period;
    uint64_t waits, failures, waitTime;
    uint64_t notifications,presents,overruns,copyTime,maximumCopy;
    uint64_t phaseSleeps,phaseSleepTime,shortPolls;
    unsigned hz,totalLines,activeLast;
    uint32_t lastPresentFrame;
    bool hasPresented;

    uint32_t reg(unsigned offset) const { return registers[offset/4]; }
    bool enabled(void) const {
        return __atomic_load_n(&qualified,__ATOMIC_ACQUIRE) && reg(0)==1 && (reg(0x26c)&1);
    }
    void disable(void) { __atomic_store_n(&qualified,0,__ATOMIC_RELEASE); }
    bool initialize(void) {
        if(gHardwareVSync!=1)return false;
        IORegistryEntry *node=IORegistryEntry::fromPath("/arm-io/marble-display-timing",gIODTPlane);
        if(!node){IOLog("marble-display: timing DT absent; hardware sync disabled\n");return false;}
        /* v1 reset before returning its first mapped sample. An Android DT
         * address is not proof that firmware left register-bus access on.
         * Firmware now leases the three display-bus clocks and verifies real
         * counter edges before publishing this qualification. It holds the
         * lease until every participating XNU core is OFF. */
        OSData *access=OSDynamicCast(OSData,node->getProperty("register-access-qualified"));
        uint32_t accessQualified=0;
        if(access && access->getLength()==sizeof(accessQualified))memcpy(&accessQualified,access->getBytesNoCopy(),sizeof(accessQualified));
        if(accessQualified!=1){
            node->release();
            IOLog("marble-display: register-bus access unqualified; no MMIO reads, hardware sync disabled\n");
            return false;
        }
        OSData *data=OSDynamicCast(OSData,node->getProperty("intf-candidates-pa"));
        OSData *selected=OSDynamicCast(OSData,node->getProperty("qualified-intf-pa"));
        uint64_t selectedPA=0;
        if(selected && selected->getLength()==sizeof(selectedPA))memcpy(&selectedPA,selected->getBytesNoCopy(),sizeof(selectedPA));
        uint64_t candidates[2]={0,0};
        if(data && data->getLength()==sizeof(candidates))memcpy(candidates,data->getBytesNoCopy(),sizeof(candidates));
        node->release();
        if(candidates[0]!=0xae36000 || candidates[1]!=0xae37000 ||
           (selectedPA!=candidates[0] && selectedPA!=candidates[1])){
            IOLog("marble-display: unsupported timing resource; hardware sync disabled\n");return false;
        }
        vm_offset_t mapped=ml_io_map(0xae34000,0x4000);
        if(!mapped){IOLog("marble-display: timing map failed\n");return false;}
        for(unsigned i=0;i<2;i++) {
            if(candidates[i]!=selectedPA)continue;
            registers=(volatile uint32_t *)(mapped+candidates[i]-0xae34000);
            uint32_t horizontal=reg(8)>>16, vertical=reg(0xc);
            IOLog("marble-display: candidate=%#llx en=%#x count_en=%#x htotal=%u vperiod=%u frame=%u line=%u\n",
                candidates[i],reg(0),reg(0xa8),horizontal,vertical,reg(0xac),reg(0xb0)&0xffff);
            if(reg(0)!=1 || !(reg(0x26c)&1) || !horizontal || vertical<=horizontal)continue;
            uint32_t previous=reg(0xac),edges=0,missed=0;
            uint64_t first=0,last=0;
            for(unsigned n=0;n<200;n++) {
                uint32_t current=reg(0xac);
                if(current!=previous) {
                    uint64_t observed=mach_absolute_time();
                    if(current-previous!=1)missed++;
                    if(!first)first=observed;
                    else {edges++;last=observed;}
                    previous=current;
                }
                IOSleep(1);
            }
            uint64_t ns=0;
            if(edges)absolutetime_to_nanoseconds((last-first)/edges,&ns);
            if(edges>=5 && !missed && ns>=NSEC_PER_SEC/150 && ns<=NSEC_PER_SEC/40) {
                period=(last-first)/edges;hz=(unsigned)((NSEC_PER_SEC+ns/2)/ns);
                totalLines=vertical/horizontal;activeLast=reg(0x24)/horizontal;
                if(!totalLines || totalLines>65535 || activeLast+8>=totalLines)continue;
                IOLog("marble-display: %s ready=1 base=%#llx edges=%u period_us=%llu hz=%u source=DSI-frame-counter polling=1 writes=0\n",
                    marble_display_abi,candidates[i],edges,ns/1000,hz);
                __atomic_store_n(&qualified,1,__ATOMIC_RELEASE);
                return true;
            }
            IOLog("marble-display: counter qualification failed edges=%u missed=%u period_ns=%llu\n",edges,missed,ns);
        }
        disable();IOLog("marble-display: no qualified video counter; hardware sync disabled\n");return false;
    }
    bool waitNext(void) {
        if(!enabled())return false;
        uint32_t before=reg(0xac);
        uint64_t begin=mach_absolute_time(),step,limit=begin+period*2;
        nanoseconds_to_absolutetime(500000,&step);
        while(mach_absolute_time()<limit) {
            if(!enabled())break;
            if(reg(0xac)!=before){waits++;waitTime+=mach_absolute_time()-begin;return true;}
            clock_delay_until(mach_absolute_time()+step);
        }
        failures++;IOLog("marble-display: counter stalled; stop hardware pacing (no invented refresh)\n");
        disable();return false;
    }
    bool waitBlank(uint64_t budget,uint32_t &frame,uint32_t &line) {
        if(!enabled())return false;
        uint64_t begin=mach_absolute_time(),limit=begin+period*2,step,guard,sleepThreshold;
        nanoseconds_to_absolutetime(250000,&step);
        nanoseconds_to_absolutetime(750000,&guard);
        nanoseconds_to_absolutetime(1000000,&sleepThreshold);
        while(mach_absolute_time()<limit) {
            if(!enabled())break;
            uint32_t before=reg(0xac),scan=reg(0xb0)&0xffff,after=reg(0xac);
            if(marble_blank_admit(before,scan,after,lastPresentFrame,hasPresented,
                activeLast,totalLines,period,budget)) {
                frame=after;line=scan;lastPresentFrame=after;hasPresented=true;
                waits++;waitTime+=mach_absolute_time()-begin;return true;
            }
            uint64_t now=mach_absolute_time();
            if(now>=limit)break;
            uint64_t delay=before==after?marble_blank_poll_delay(after,scan,lastPresentFrame,
                hasPresented,activeLast,totalLines,period,budget,step,guard):step;
            if(delay>limit-now)delay=limit-now;
            if(delay>=sleepThreshold) {
                /* show/displayPower already use blocking waits with this
                 * mutex. Explicitly block for the coarse phase wait, without
                 * relying on a platform idle-latency spin threshold. */
                phaseSleeps++;
                if(assert_wait_deadline((event_t)this,THREAD_UNINT,now+delay)==THREAD_WAITING)
                    thread_block(THREAD_CONTINUE_NULL);
                phaseSleepTime+=mach_absolute_time()-now;
            } else {shortPolls++;clock_delay_until(now+delay);}
        }
        failures++;return false;
    }
    void copied(uint32_t frame,uint32_t line,uint64_t elapsed) {
        uint32_t endFrame=reg(0xac),endLine=reg(0xb0)&0xffff;
        bool blank=marble_blank_copy_completed(frame,line,endFrame,endLine,activeLast,totalLines);
        presents++;copyTime+=elapsed;if(elapsed>maximumCopy)maximumCopy=elapsed;
        if(!blank)overruns++;
        if(presents<=8 || presents%60==0 || !blank) {
            uint64_t ns;absolutetime_to_nanoseconds(elapsed,&ns);
            IOLog("marble-present: seq=%llu frame=%u begin_line=%u end_frame=%u end_line=%u active_last=%u total_lines=%u copy_us=%llu blank=%u overruns=%llu\n",
                presents,frame,line,endFrame,endLine,activeLast,totalLines,ns/1000,(unsigned)blank,overruns);
        }
    }
    void report(void) {
        if(!enabled())return;
        uint64_t ns;absolutetime_to_nanoseconds(maximumCopy,&ns);
        uint64_t sleepNs;absolutetime_to_nanoseconds(phaseSleepTime,&sleepNs);
        IOLog("marble-display-wait: phase-v1 sleeps=%llu sleep_us=%llu short_polls=%llu admission=counter-and-line no_irq=1\n",
            phaseSleeps,sleepNs/1000,shortPolls);
        IOLog("marble-display: runtime ready=1 hz=%u frame=%u waits=%llu failures=%llu notifications=%llu presents=%llu overruns=%llu max_copy_us=%llu source=DSI-frame-counter polling=1\n",
            hz,reg(0xac),waits,failures,notifications,presents,overruns,ns/1000);
    }
};
static MarbleDisplayTiming gMarbleTiming;
