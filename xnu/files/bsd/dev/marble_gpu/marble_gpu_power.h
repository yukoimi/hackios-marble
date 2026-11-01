/* Marble CX-only diagnostic transaction. Register facts and timing:
 * Xiaomi 48952ed3 drivers/clk/qcom/gdsc-regulator.c, gpucc-waipio.c,
 * Mu-marble DT gdsc@3d99108 (hw-ctrl@3d9953c, retain-regs).
 * This is not sheng's board implementation. No GMU/GX/SMMU programming.
 */
#ifndef MARBLE_GPU_POWER_H
#define MARBLE_GPU_POWER_H
#include <stdint.h>
#define MARBLE_GPU_POWER_ABI "MGPU-MARBLE-POWER-v1"
enum { MGP_CX=0x9108,MGP_CX_STATUS=0x953c,MGP_GX=0x905c,
       MGP_AHB=0x911c,MGP_SMMU=0x7000,MGP_GMU=0x913c,MGP_HUB=0x9148 };
struct marble_gpu_power_io {
    void *cookie;
    uint32_t (*read)(void *,unsigned);
    void (*write)(void *,unsigned,uint32_t);
    void (*delay)(void *,unsigned);
    int (*observe)(void *);
};
struct marble_gpu_power_result {
    uint32_t cx_before,cx_status_before,gx_before,ahb_before,smmu_before;
    uint32_t cx_on,clocks_on,observed,restored,skipped;
    uint32_t cx_after,cx_status_after,ahb_after,smmu_after;
};
static int marble_gpu_power_poll(const struct marble_gpu_power_io *io,
    unsigned reg,uint32_t mask,uint32_t value)
{
    for(unsigned us=0;us<=500;us+=5) {
        if((io->read(io->cookie,reg)&mask)==value)return 1;
        io->delay(io->cookie,5);
    }
    return 0;
}
static void marble_gpu_power_transaction(const struct marble_gpu_power_io *io,
    struct marble_gpu_power_result *r)
{
    *r=(struct marble_gpu_power_result){0};
    void *c=io->cookie;
    r->cx_before=io->read(c,MGP_CX);r->cx_status_before=io->read(c,MGP_CX_STATUS);
    r->gx_before=io->read(c,MGP_GX);r->ahb_before=io->read(c,MGP_AHB);
    r->smmu_before=io->read(c,MGP_SMMU);
    /* Refuse pre-existing ownership, HW control or indeterminate power state.
     * Only the observed stock state (CX/GX collapsed and no local clock vote)
     * is eligible. No reset/voltage/parent clock changes are made here. */
    if(!(r->cx_before&1) || !(r->gx_before&1) ||
       (r->cx_before&0x80000002u) || (r->cx_status_before&0x80000000u) ||
       (r->gx_before&0x80000000u) || (r->ahb_before&1) || (r->smmu_before&1)) {
        r->skipped=1;return;
    }
    io->write(c,MGP_CX,r->cx_before&~1u);io->delay(c,1);
    r->cx_on=marble_gpu_power_poll(io,MGP_CX_STATUS,0x80000000u,0x80000000u);
    if(r->cx_on) {
        io->write(c,MGP_CX,(r->cx_before&~1u)|(1u<<11));io->delay(c,2);
        io->write(c,MGP_AHB,r->ahb_before|1);io->delay(c,1);
        io->write(c,MGP_SMMU,r->smmu_before|1);
        r->clocks_on=marble_gpu_power_poll(io,MGP_SMMU,0x80000000u,0) &&
            !(io->read(c,MGP_AHB)&0x80000000u) &&
            !(io->read(c,MGP_GMU)&0x80000000u) &&
            !(io->read(c,MGP_HUB)&0x80000000u);
        if(r->clocks_on && io->observe)r->observed=io->observe(c)==0;
        /* Restore local votes while CX is still powered. */
        io->write(c,MGP_SMMU,r->smmu_before);
        io->write(c,MGP_AHB,r->ahb_before);io->delay(c,2);
    }
    /* Always roll back the collapse and retain-FF bits, even on timeout.
     * DT asks Linux to delay 100us after disable rather than rely on status. */
    io->write(c,MGP_CX,r->cx_before);io->delay(c,100);
    r->cx_after=io->read(c,MGP_CX);r->cx_status_after=io->read(c,MGP_CX_STATUS);
    r->ahb_after=io->read(c,MGP_AHB);r->smmu_after=io->read(c,MGP_SMMU);
    r->restored=((r->cx_after^r->cx_before)&0x7fffffffu)==0 &&
        ((r->cx_status_after^r->cx_status_before)&0x80000000u)==0 &&
        ((r->ahb_after^r->ahb_before)&1)==0 &&
        ((r->smmu_after^r->smmu_before)&1)==0;
}
#endif
