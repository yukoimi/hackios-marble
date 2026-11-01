/* Read-only TSENS v2 sampling. Register layout and deci-Celsius encoding:
 * MiCode marble kernel 48952ed36228 drivers/thermal/qcom/tsens{,-v2}.c.
 * Exact two TM/SROT addresses and CPU/GPU channels come from marble.dtb.
 * This does not enable sensors, alter trips, voltage, clocks or charging. */
#ifndef XNULOADER_MARBLE_THERMAL_H
#define XNULOADER_MARBLE_THERMAL_H
#include <stdint.h>

typedef int (*MarbleThermalRead)(void *,uint64_t,uint32_t *);
typedef struct {
    unsigned valid,controller,sensor;
    int maximum_mc;
    uint32_t status;
} MarbleThermalSample;

static int marble_thermal_snapshot(MarbleThermalRead read,void *cookie,MarbleThermalSample *out)
{
    static const uint64_t tm[2]={0x0c263000,0x0c265000};
    static const uint64_t srot[2]={0x0c222000,0x0c223000};
    MarbleThermalSample r={0,0,0,-40000,0};
    for(unsigned controller=0;controller<2;controller++) {
        uint32_t version=0,control=0,critical=0;
        if(read(cookie,srot[controller],&version) || (version>>28)!=2 ||
           read(cookie,srot[controller]+4,&control) || (control&3)!=1 ||
           read(cookie,tm[controller]+0x14,&critical) || (critical&0xffffu) ||
           (critical&0x80000000u)) { *out=r;return 0; }
        /* Follow get_temp_tsens_valid(): per-channel VALID qualifies a sample.
         * TRDY is not used by that path. CC_MON (bit 30) is a cycle-monitor
         * interrupt status, not a temperature fault or watchdog bark (bit 31). */
        /* Controller 0: CPU1..13 and GPU14..15. Controller 1: little CPU1..4. */
        for(unsigned sensor=1;sensor<=(controller?4:15);sensor++) {
            uint32_t status=0;unsigned retry;
            for(retry=0;retry<3;retry++) {
                if(read(cookie,tm[controller]+0xa0+4*sensor,&status))break;
                if(status&(1u<<21))break;
            }
            if(retry==3 || !(status&(1u<<21)) || (status&(1u<<19))) { *out=r;return 0; }
            int deci=(int)(status&0xfffu);if(deci&0x800)deci-=0x1000;
            int mc=deci*100;
            if(mc < -40000 || mc > 125000) { *out=r;return 0; }
            if(mc>r.maximum_mc) {
                r.maximum_mc=mc;r.controller=controller;r.sensor=sensor;r.status=status;
            }
            r.valid++;
        }
    }
    *out=r;return r.valid==19;
}
#endif
