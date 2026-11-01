/* Read-only adequacy checks for firmware-retained APSS requests. These are
 * protocol values and dependency levels, never physical voltage telemetry. */
#ifndef MARBLE_GMU_DEPENDENCIES_H
#define MARBLE_GMU_DEPENDENCIES_H
#include "marble_gmu_preflight.h"
#define MARBLE_GMU_DEPENDENCIES_ABI "MGPU-MARBLE-GMU-DEPENDENCIES-v1"
static int marble_arc_level(const MarbleCmdDbEntry *e,uint32_t response,uint32_t *level)
{
    if(!e || !level || e->slave!=3 || e->bytes!=32 || response>15)return 0;
    uint32_t v=marble_db_u16(e->aux+response*2);
    if(response && !v)return 0;
    *level=v;return 1;
}
static int marble_bcm_covers(uint32_t present,uint32_t minimum)
{
    /* Compare the two 14-bit votes independently. COMMIT is a command bit;
     * VALID must be set for a nonzero minimum. ACV's bit mask is handled
     * separately because it does not encode a frequency. */
    return (present&0x20000000u) &&
        ((present>>14)&0x3fff)>=((minimum>>14)&0x3fff) &&
        (present&0x3fff)>=(minimum&0x3fff);
}
#endif
