/* Marble: CB0 context audit and stage7-only SID5/TBU ECATS transaction.
 * GMU remains reset, GX remains collapsed. Uses
 * pinned UniXNU context/program/stream-audit/physical-table-audit primitives.
 */
#include <libkern/libkern.h>
#include <kern/clock.h>
#include <mach/mach_types.h>
#include "marble_gpu/marble_smmu.h"
#include "marble_gpu/upstream/ArmSmmu4KAudit.h"
#include "marble_gpu/upstream/ArmSmmu4KStreams.h"
#include "marble_gpu/marble_ecats.h"
#include "marble_gpu/marble_gmu_memory.h"
#include "marble_gpu/marble_gmu_resources.h"
#include "marble_gpu/marble_gpu_context.h"

extern vm_offset_t ml_io_map(vm_offset_t,vm_size_t);
extern void marble_gpu_log(const char *,...) __printflike(1,2);
extern kern_return_t gic_v3_spi_attach(uint32_t,bool,void (*)(void *,uint32_t),void *);
extern void gic_v3_spi_detach(uint32_t);
extern void gic_v3_spi_mask(uint32_t);

/* Mu DT kgsl-smmu interrupts[0]=SPI673 global; [1]=SPI678 CB0. */
#define SMMU_GLOBAL_IRQ (673u+32)
#define SMMU_CB0_IRQ (678u+32)
struct probe_io {vm_offset_t base;volatile uint32_t irq_fault;unsigned writes,ats_pending,stage,stream_lease,bank;uint32_t irq_fsr,irq_syn,irq_sid;uint64_t irq_far;};
static int rd32(void *cookie,size_t at,uint32_t *out)
{
    struct probe_io *p=cookie;if(!out || (at&3) || at>0x3fffc)return 1;
    __asm__ volatile("ldr %w0, [%1]":"=r"(*out):"r"(p->base+at):"memory");
    if(at==MARBLE_SMMU_CB0+p->bank*4096+0x7f4 || at==MARBLE_SMMU_CB0+p->bank*4096+0x8f0)delay(1);
    return 0;
}
static int rd64(void *cookie,size_t at,uint64_t *out)
{
    struct probe_io *p=cookie;if(!out || (at&7) || at>0x3fff8)return 1;
    __asm__ volatile("ldr %0, [%1]":"=r"(*out):"r"(p->base+at):"memory");return 0;
}
static int wr32(void *cookie,size_t at,uint32_t value)
{
    struct probe_io *p=cookie;
    if(p->bank==1? !marble_gpu_smmu_write_allowed(at,4,value,p->stream_lease):
        (!marble_smmu_write_allowed(at,4) && !marble_smmu_stream_write_allowed(at,4,value,p->stage,p->stream_lease)))return 1;
    marble_gpu_log("marble-smmu: write32 offset=%#lx value=%#x\n",(unsigned long)at,value);
    /* Qualcomm hypervisor decodes the instruction. Never emit STR WZR. */
    __asm__ volatile("mov w9, %w0\n\tstr w9, [%1]\n\tdsb sy"::"r"(value),"r"(p->base+at):"x9","memory");
    p->writes++;return 0;
}
static int wr64(void *cookie,size_t at,uint64_t value)
{
    struct probe_io *p=cookie;
    if(p->bank==1?!marble_gpu_smmu_write_allowed(at,8,0,0):!marble_smmu_write_allowed(at,8))return 1;
    if(at==MARBLE_SMMU_CB0+0x800) {
        uint32_t id0;if(rd32(cookie,0x20,&id0) || !marble_smmu_has_atos(id0))return 1;
    }
    marble_gpu_log("marble-smmu: write64 offset=%#lx value=%#llx\n",(unsigned long)at,value);
    __asm__ volatile("mov x9, %0\n\tstr x9, [%1]\n\tdsb sy"::"r"(value),"r"(p->base+at):"x9","memory");
    p->writes++;return 0;
}
static int barrier(void *cookie)
{(void)cookie;__asm__ volatile("dsb sy":::"memory");return 0;}
static void fault_irq(void *cookie,uint32_t intid)
{
    struct probe_io *p=cookie;
    size_t cb=MARBLE_SMMU_CB0+p->bank*4096;
    if(p->bank==1 && !p->irq_fault) {
        rd32(p,cb+0x58,&p->irq_fsr);rd32(p,cb+0x68,&p->irq_syn);
        rd32(p,0x1400+p->bank*4,&p->irq_sid);rd64(p,cb+0x60,&p->irq_far);
    }
    p->irq_fault=1;
    /* Keep the source intact for the diagnostic thread; stop a level storm. */
    gic_v3_spi_mask(intid);
}
struct table_io {const ArmSmmu4KConfig *config;uint64_t *tables;size_t capacity;};
static int read_descriptor(void *cookie,uint64_t pa,uint64_t *out)
{
    struct table_io *p=cookie;uint64_t base=p->config->table_physical;
    if(!out || (pa&7) || pa<base || pa-base>=p->capacity*4096)return 1;
    *out=*(volatile uint64_t *)((uint8_t *)p->tables+(size_t)(pa-base));return 0;
}
static void clean_tables(uint64_t *tables,size_t capacity)
{
    uint64_t ctr;__asm__ volatile("mrs %0, ctr_el0":"=r"(ctr));
    size_t line=(size_t)4<<((ctr>>16)&15);
    for(vm_offset_t va=(vm_offset_t)tables;va<(vm_offset_t)tables+capacity*4096;va+=line)
        __asm__ volatile("dc cvac, %0"::"r"(va):"memory");
    __asm__ volatile("dsb sy":::"memory");
}
static int tbu_audit(void)
{
    /* Pinned Xiaomi arm-smmu-qcom.c arm_tbu_impl; no debug writes here. */
    marble_gpu_log("marble-tbu: MGPU-MARBLE-TBU-v1 read audit begin; no context/stream/TBU writes\n");
    vm_offset_t tcu=ml_io_map(0x03de6000,0x20),tbu=ml_io_map(0x03de9000,0x1000);
    vm_offset_t sr=ml_io_map(0x03de6200,8);
    if(!tcu || !tbu || !sr)return -1;
    uint32_t version,ack;uint64_t status,halt=0,trigger=0,value;
    marble_gpu_log("marble-tbu: reading TCU version +0x18\n");
    __asm__ volatile("ldr %w0, [%1]":"=r"(version):"r"(tcu+0x18):"memory");
    marble_gpu_log("marble-tbu: reading gfx0 external status\n");
    __asm__ volatile("ldr %0, [%1]":"=r"(status):"r"(sr):"memory");
    static const unsigned offsets[]={0,8,0x18,0x28,0x30};
    for(unsigned i=0;i<sizeof(offsets)/sizeof(offsets[0]);i++) {
        unsigned at=offsets[i];
        marble_gpu_log("marble-tbu: reading gfx0 +%#x\n",at);
        __asm__ volatile("ldr %0, [%1]":"=r"(value):"r"(tbu+at):"memory");
        marble_gpu_log("marble-tbu: gfx0 +%#x=%#llx\n",at,value);
        if(!at)halt=value;if(at==0x18)trigger=value;
    }
    __asm__ volatile("ldr %w0, [%1]":"=r"(ack):"r"(tbu+0x20):"memory");
    int idle=!(halt&(1u<<16)) && !(trigger&1) && !(ack&3);
    marble_gpu_log("marble-tbu: TCU version=%#x status=%#llx ACK=%#x idle=%d\n",version,status,ack,idle);
    return idle?0:-1;
}
static int translation(const ArmSmmu4KIo *io,uint64_t iova,uint64_t expected,int fault)
{
    struct probe_io *p=io->cookie;
    uint64_t par=0;uint32_t status;
    p->ats_pending=1;
    if(io->write64(io->cookie,MARBLE_SMMU_CB0+0x800,iova&~UINT64_C(4095)))return 0;
    for(unsigned i=0;i<500;i++) {
        if(io->read32(io->cookie,MARBLE_SMMU_CB0+0x8f0,&status))return 0;
        if(!(status&1)) {
            p->ats_pending=0;
            if(io->barrier(io->cookie) || io->read64(io->cookie,MARBLE_SMMU_CB0+0x50,&par))return 0;
            int ok=fault?!!(par&1):!(par&1) &&
                ((par&UINT64_C(0x0000fffffffff000))|(iova&4095))==expected;
            marble_gpu_log("marble-smmu: hardware ATS iova=%#llx PAR=%#llx expected=%#llx fault=%d PASS=%d\n",
                iova,par,expected,fault,ok);return ok;
        }
    }
    marble_gpu_log("marble-smmu: hardware ATS TIMEOUT; retain tables\n");return 0;
}
struct tbu_io {vm_offset_t base;};
static int tbu_rd32(void *cookie,size_t at,uint32_t *out)
{
    struct tbu_io *p=cookie;if(!out || at!=0x20)return 1;
    __asm__ volatile("ldr %w0, [%1]":"=r"(*out):"r"(p->base+at):"memory");delay(1);return 0;
}
static int tbu_rd64(void *cookie,size_t at,uint64_t *out)
{
    struct tbu_io *p=cookie;
    if(!out || (at!=0 && at!=8 && at!=0x18 && at!=0x28 && at!=0x30))return 1;
    __asm__ volatile("ldr %0, [%1]":"=r"(*out):"r"(p->base+at):"memory");return 0;
}
static int tbu_wr64(void *cookie,size_t at,uint64_t value)
{
    struct tbu_io *p=cookie;if(!marble_tbu_write_allowed(at,8))return 1;
    marble_gpu_log("marble-ecats: write64 offset=%#lx value=%#llx\n",(unsigned long)at,value);
    __asm__ volatile("mov x9, %0\n\tstr x9, [%1]\n\tdsb sy"::"r"(value),"r"(p->base+at):"x9","memory");return 0;
}
static int ecats_translation(const ArmSmmu4KIo *smmu,const ArmSmmu4KIo *tbu,
    uint64_t iova,uint64_t expected,int fault,unsigned attempts,uint32_t fsr_background,
    unsigned access,uint32_t expected_fsr)
{
    for(unsigned attempt=0;attempt<attempts;attempt++) {
        MarbleEcatsResult r;int status=marble_ecats_access(tbu,iova,access,500,&r);
        uint32_t fsr,gfsr,after;
        rd32(smmu->cookie,MARBLE_SMMU_CB0+0x58,&fsr);rd32(smmu->cookie,0x48,&gfsr);
        int ok=!status && r.completed && (fault?!!(r.par&1):!(r.par&1) && r.physical==expected);
        ok=ok && (fsr&0xc00001fe)==expected_fsr;
        marble_gpu_log("marble-ecats: " MARBLE_ECATS_ABI " iova=%#llx PAR=%#llx PA=%#llx expected=%#llx fault=%d attempt=%u status=%d complete=%u restored=%u retain=%u FSR=%#x GFSR=%#x access=%u expected_fsr=%#x PASS=%d\n",
            iova,r.par,r.physical,expected,fault,attempt+1,status,r.completed,r.restored,r.retain,fsr,gfsr,access,expected_fsr,ok);
        if(r.retain || !r.restored || gfsr || (fsr&(1u<<30)) ||
            (fsr&~UINT32_C(0xc00001fe))!=fsr_background)
            panic("marble-ecats: TBU/fault cleanup unverified; retain RAM, reset required");
        /* Only clear the defined fault bits actually observed in our lease.
         * SS is refused above: do not invent a stalled-transaction recovery. */
        if(fsr&0xc00001fe)wr32(smmu->cookie,MARBLE_SMMU_CB0+0x58,fsr&0xc00001fe);
        barrier(smmu->cookie);rd32(smmu->cookie,MARBLE_SMMU_CB0+0x58,&after);
        if((after&0xc00001fe) || (after&~UINT32_C(0xc00001fe))!=fsr_background)
            panic("marble-ecats: context fault not cleared; retain RAM, reset required");
        if(ok)return 1;
        if(status || fault || !(r.par&1))return 0;
        /* Pinned Xiaomi quirk: up to two retries after a negative result. */
    }
    return 0;
}
static int ecats_test(const ArmSmmu4KIo *io,const ArmSmmu4KRange *ranges,size_t count,unsigned stage)
{
    struct tbu_io p={ml_io_map(0x03de9000,0x1000)};if(!p.base)return 0;
    ArmSmmu4KIo tbu={&p,0x1000,tbu_rd32,tbu_rd64,NULL,tbu_wr64,barrier};
    uint32_t sctlr,value,fsr;
    rd32(io->cookie,MARBLE_SMMU_CB0,&sctlr);rd32(io->cookie,MARBLE_SMMU_CB0+0x58,&fsr);
    if(fsr&0xc00001fe)return 0;
    uint32_t background=fsr&~UINT32_C(0xc00001fe),masked=sctlr&~UINT32_C(0xc0);
    wr32(io->cookie,MARBLE_SMMU_CB0,masked);rd32(io->cookie,MARBLE_SMMU_CB0,&value);
    if(value!=masked)panic("marble-ecats: fault mask did not read back; reset required");
    int passed=1;
    if(stage==7) {
        uint64_t data_pa=ranges[0].physical;
        passed=ecats_translation(io,&tbu,0x60000000,data_pa,0,1,background,0,0) &&
            ecats_translation(io,&tbu,0x60000fff,data_pa+4095,0,1,background,0,0) &&
            ecats_translation(io,&tbu,0x60001000,0,1,1,background,0,2) &&
            ecats_translation(io,&tbu,0x60000000,data_pa,0,3,background,0,0);
    } else {
        size_t pages=0,boundaries=0,writes=0,fetches=0,denied=0,guards=0,recovered=0;
        for(size_t i=0;i<count && passed;i++) {
            const ArmSmmu4KRange *r=ranges+i;
            for(uint64_t offset=0;offset<r->bytes && passed;offset+=4096) {
                passed=ecats_translation(io,&tbu,r->iova+offset,r->physical+offset,0,3,background,1,0) &&
                    ecats_translation(io,&tbu,r->iova+offset+4095,r->physical+offset+4095,0,1,background,1,0);
                if(passed) {pages++;boundaries+=2;}
            }
            if(passed) {passed=ecats_translation(io,&tbu,r->iova,r->physical,0,1,background,3,0);writes+=passed;}
            unsigned nx=!(r->permissions&ArmSmmu4KExecute);
            if(passed) {passed=ecats_translation(io,&tbu,r->iova,r->physical,nx,1,background,5,nx?8:0);fetches+=passed;}
            if(passed) {passed=ecats_translation(io,&tbu,r->iova,0,1,1,background,0,8);denied+=passed;}
            uint64_t end=r->iova+r->bytes;
            if(passed && (i+1==count || end<ranges[i+1].iova)) {
                passed=ecats_translation(io,&tbu,end,0,1,1,background,1,2);guards+=passed;
            }
            if(passed) {passed=ecats_translation(io,&tbu,r->iova,r->physical,0,3,background,1,0);recovered+=passed;}
        }
        if(passed) {passed=ecats_translation(io,&tbu,0,0,1,1,background,1,2);guards+=passed;}
        if(passed)passed=ecats_translation(io,&tbu,ranges[0].iova,ranges[0].physical,0,3,background,1,0);
        size_t wanted_pages=stage>=11?59:51,wanted_ranges=stage>=11?8:6;
        passed=passed && pages==wanted_pages && boundaries==2*wanted_pages && writes==wanted_ranges &&
            fetches==wanted_ranges && denied==wanted_ranges && guards==6 && recovered==wanted_ranges;
        marble_gpu_log("marble-gmu-memory: ECATS pages=%lu boundaries=%lu writes=%lu fetches=%lu denied=%lu guards=%lu recovered=%lu PASS=%d\n",
            (unsigned long)pages,(unsigned long)boundaries,(unsigned long)writes,(unsigned long)fetches,
            (unsigned long)denied,(unsigned long)guards,(unsigned long)recovered,passed);
    }
    wr32(io->cookie,MARBLE_SMMU_CB0,sctlr);rd32(io->cookie,MARBLE_SMMU_CB0,&value);
    if(value!=sctlr)panic("marble-ecats: original SCTLR not restored; reset required");
    return passed;
}
int marble_smmu_probe(vm_offset_t base,unsigned stage,const ArmSmmu4KConfig *lower,
    const ArmSmmu4KResult *result,uint64_t *tables,const ArmSmmu4KRange *ranges,size_t count,size_t capacity,
    int (*work)(void *),void *work_cookie);
int marble_smmu_probe(vm_offset_t base,unsigned stage,const ArmSmmu4KConfig *lower,
    const ArmSmmu4KResult *result,uint64_t *tables,const ArmSmmu4KRange *ranges,size_t count,size_t capacity,
    int (*work)(void *),void *work_cookie)
{
    if(!lower || !result || !tables || !ranges || !count || count>(stage>=11?MARBLE_GMU_RESOURCE_RANGE_CAPACITY:MARBLE_GMU_RANGE_CAPACITY) ||
        !capacity || capacity>MARBLE_GMU_TABLE_CAPACITY || result->table_pages>capacity)return -1;
    struct probe_io p={base,0,0,0,stage,0};
    ArmSmmu4KIo io={&p,0x40000,rd32,rd64,wr32,wr64,barrier};
    marble_gpu_log("marble-smmu: " MARBLE_SMMU_ABI " stage=%u; GR1/CB read audit begin\n",stage);
    unsigned active=0,asid_conflict=0;uint32_t value;
    /* DT exposes 21 nonsecure contexts; hardware reports 22. Exclude CB21. */
    for(unsigned bank=0;bank<21;bank++) {
        uint32_t cbar,cba2r,sctlr,actlr,fsr;uint64_t ttbr;
        size_t cb=MARBLE_SMMU_CB0+bank*4096;
        marble_gpu_log("marble-smmu: reading bank=%u GR1/CB\n",bank);
        rd32(&p,0x1000+4*bank,&cbar);rd32(&p,0x1800+4*bank,&cba2r);
        rd32(&p,cb,&sctlr);rd32(&p,cb+4,&actlr);rd64(&p,cb+0x20,&ttbr);rd32(&p,cb+0x58,&fsr);
        marble_gpu_log("marble-smmu: bank=%u CBAR=%#x CBA2R=%#x SCTLR=%#x ACTLR=%#x TTBR0=%#llx FSR=%#x\n",
            bank,cbar,cba2r,sctlr,actlr,ttbr,fsr);
        active+=!!(sctlr&1);asid_conflict+=((ttbr>>48)==MARBLE_SMMU_ASID);
    }
    vm_offset_t gmu=ml_io_map(0x03d68000,0x37000);
    if(!gmu)return -1;
    uint32_t reset,fw_result;
    __asm__ volatile("ldr %w0, [%1]":"=r"(reset):"r"(gmu+0x16000):"memory");
    __asm__ volatile("ldr %w0, [%1]":"=r"(fw_result):"r"(gmu+0x16070):"memory");
    marble_gpu_log("marble-smmu: GMU SYSRESET=%#x FW_INIT_RESULT=%#x active_banks=%u asid_conflicts=%u\n",
        reset,fw_result,active,asid_conflict);
    if(stage<5)return 0;
    if(stage>=6) {int status=tbu_audit();if(status || stage==6)return status;}
    if(reset!=1 || active || asid_conflict) {
        marble_gpu_log("marble-smmu: context test REFUSED: master/bank/ASID not idle\n");return -1;
    }
    /* This single XNU init path is the only board GPU driver; CPU0 only
     * handles EFI USB/touch. The powered-off GX and reset GMU cannot DMA.
     * Require *every* stream faulting and invalid before taking CB0/ASID42. */
    for(unsigned slot=0;slot<24;slot++) {
        uint32_t smr,s2cr;rd32(&p,0x800+4*slot,&smr);rd32(&p,0xc00+4*slot,&s2cr);
        if(smr || s2cr!=0x20000) {marble_gpu_log("marble-smmu: stream lease REFUSED slot=%u\n",slot);return -1;}
    }
    ArmSmmu4KContextConfig context={lower,NULL,result,NULL,0x4c017e18,0x5511,MARBLE_SMMU_ASID,1,0,0};
    ArmSmmu4KStream route[2]={{0,5},{1,0x405}};unsigned route_count=stage==17?2:1;ArmSmmu4KStreamResult streams;
    ArmSmmu4KStatus status=ArmSmmu4KCheckFaultedStreams(&io,route,route_count,0,&streams);
    if(status) {marble_gpu_log("marble-smmu: global/stream policy refused status=%u\n",status);return -1;}
    unsigned char visited[MARBLE_GMU_TABLE_CAPACITY];struct table_io tableio={lower,tables,capacity};
    ArmSmmu4KAuditRegion region={ranges,count,capacity,capacity*512,visited,sizeof(visited)};
    ArmSmmu4KAuditConfig audit={{&tableio,read_descriptor},&region,NULL};
    ArmSmmu4KAuditResult audited;
    status=ArmSmmu4KAuditDeclaredContext(&context,&audit,&audited);
    if(status)return -1;
    /* Hardware audit found uninitialized TTBR/TCR/SCTLR fields in disabled
     * banks. Like Linux's unassigned-bank path, require M=0, then initialize
     * only this leased bank. Do not republish uninitialized table addresses. */
    uint32_t old_cbar,old_cba2r,old_actlr,sctlr,fsr;
    rd32(&p,0x1000,&old_cbar);rd32(&p,0x1800,&old_cba2r);
    rd32(&p,MARBLE_SMMU_CB0+4,&old_actlr);
    rd32(&p,MARBLE_SMMU_CB0,&sctlr);rd32(&p,MARBLE_SMMU_CB0+0x58,&fsr);
    if((sctlr&1) || (fsr&0xc00001fe))return -1;
    ArmSmmu4KContextImage image;
    if(ArmSmmu4KEncodeContext(&context,&image))return -1;
    if(gic_v3_spi_attach(SMMU_GLOBAL_IRQ,false,fault_irq,&p)!=KERN_SUCCESS)return -1;
    if(gic_v3_spi_attach(SMMU_CB0_IRQ,false,fault_irq,&p)!=KERN_SUCCESS) {
        gic_v3_spi_detach(SMMU_GLOBAL_IRQ);return -1;
    }
    clean_tables(tables,capacity);
    /* Mu DT qcom,actlr={SID0, mask0x7ff, value0x32b}, covering GMU SID5. */
    wr32(&p,MARBLE_SMMU_CB0+4,0x32b);
    rd32(&p,MARBLE_SMMU_CB0+4,&value);
    ArmSmmu4KProgramResult program={0};
    status=value==0x32b?ArmSmmu4KProgramDisabledContext(&io,0,&context,500,&program):ArmSmmu4KStateMismatch;
    marble_gpu_log("marble-smmu: program status=%u phase=%u enabled=%u retain=%u\n",
        status,program.phase,program.enabled,program.must_retain_tables);
    /* A refusal before any library write cannot have published our tables. */
    int passed=0,ats=-1,safe=(!program.writes && !program.enabled) || !program.must_retain_tables;
    if(status==ArmSmmu4KSuccess) {
        status=ArmSmmu4KObserveAuditedContext(&io,0,&context,&audit,&audited);
        marble_gpu_log("marble-smmu: active physical-table audit status=%u reads=%lu leaves=%lu\n",
            status,(unsigned long)audited.lower.descriptor_reads,(unsigned long)audited.lower.leaf_pages);
        passed=!status;
        if(stage>=7 && passed) {
            ArmSmmu4KStreamState state={0};p.stream_lease=1;
            status=ArmSmmu4KAttachStreams(&io,&state,route,route_count,0,&context,&streams);
            marble_gpu_log("marble-ecats: stream attach status=%u phase=%u attached=%u poisoned=%u\n",
                status,streams.phase,state.attached,state.poisoned);
            if(status)panic("marble-ecats: stream attachment failed; retain RAM, reset required");
            ats=ecats_test(&io,ranges,count,stage);passed=ats;
            if(passed && work) {
                int status=work(work_cookie);
                if(status==-2)panic("marble GMU master still owns SMMU lease; reset required");
                passed=status==0;
            }
            status=ArmSmmu4KFaultStreams(&io,&state,&streams);
            if(status)panic("marble-ecats: stream quarantine failed; retain RAM, reset required");
            for(unsigned i=0;i<route_count;i++) {
                rd32(&p,0xc00+4*i,&value);if(value!=0x20000)panic("marble-ecats: owned SID not faulting");
                wr32(&p,0x800+4*i,0);rd32(&p,0x800+4*i,&value);
                if(value)panic("marble-ecats: owned SID route still valid; reset required");
            }
            p.stream_lease=0;
            marble_gpu_log("marble-ecats: SID5 quarantined, SMR0 invalid, stream lease released\n");
            if(route_count==2)marble_gpu_log("marble-gmu-streams: exact SID5+SID405 quarantined and both SMRs invalid\n");
        } else if(!marble_smmu_has_atos(program.id0))
            marble_gpu_log("marble-smmu: hardware ATS SKIPPED: IDR0.ATOSNS=1; need Qualcomm ECATS, no hardware translation proven\n");
        else if(passed) {
            ats=translation(&io,0x60000000,ranges[0].physical,0) &&
                translation(&io,0x60000fff,ranges[0].physical+4095,0) && translation(&io,0x60001000,0,1);
            passed=ats;
        }
        ArmSmmu4KProgramResult stopped;
        ArmSmmu4KStatus stop=ArmSmmu4KDisableOwnedContext(&io,0,&context,500,&stopped);
        safe=!stop && stopped.disabled_verified && stopped.tlb_synchronized && !stopped.must_retain_tables;
        /* TLBSYNC alone does not prove a timed-out ATS request has completed. */
        if(p.ats_pending)safe=0;
        rd32(&p,MARBLE_SMMU_CB0+0x58,&value);
        marble_gpu_log("marble-smmu: post-ATS FSR=%#x pending=%u\n",value,p.ats_pending);
        if(value&0xc00001fe) {passed=0;safe=0;}
        marble_gpu_log("marble-smmu: disable status=%u verified=%u tlb_sync=%u retain=%u\n",
            stop,stopped.disabled_verified,stopped.tlb_synchronized,stopped.must_retain_tables);
    }
    if(safe) {
        if(program.writes) {
            /* Disable/TLBSYNC has completed. Clear both owned TTBRs in known
             * AArch64 format before giving the disabled bank back. TCR/MAIR
             * stay initialized, not restored to their power-on unknown bits. */
            wr32(&p,0x1800,image.cba2r);
            wr32(&p,MARBLE_SMMU_CB0+0x10,image.tcr2);
            wr32(&p,MARBLE_SMMU_CB0+0x30,image.tcr);
            wr64(&p,MARBLE_SMMU_CB0+0x20,0);wr64(&p,MARBLE_SMMU_CB0+0x28,0);
            uint64_t ttbr;rd64(&p,MARBLE_SMMU_CB0+0x20,&ttbr);if(ttbr)safe=0;
            rd64(&p,MARBLE_SMMU_CB0+0x28,&ttbr);if(ttbr)safe=0;
            rd32(&p,MARBLE_SMMU_CB0,&value);if(value)safe=0;
            /* CBAR=0 selects stage2 translation, forbidden to this EL1
             * guest (hardware abort observed). Keep initialized stage1/
             * AArch64 format on the released, disabled, zero-root bank. */
        }
        wr32(&p,MARBLE_SMMU_CB0+4,old_actlr);
        rd32(&p,0x1000,&value);if(value!=(program.writes?image.cbar:old_cbar))safe=0;
        rd32(&p,0x1800,&value);if(value!=(program.writes?image.cba2r:old_cba2r))safe=0;
        rd32(&p,MARBLE_SMMU_CB0+4,&value);if(value!=old_actlr)safe=0;
        status=ArmSmmu4KCheckFaultedStreams(&io,route,route_count,0,&streams);
        if(status)safe=0;
        for(unsigned slot=0;slot<24;slot++) {
            uint32_t smr,s2cr;rd32(&p,0x800+4*slot,&smr);rd32(&p,0xc00+4*slot,&s2cr);
            if(smr || s2cr!=0x20000)safe=0;
        }
    }
    gic_v3_spi_detach(SMMU_CB0_IRQ);gic_v3_spi_detach(SMMU_GLOBAL_IRQ);
    if(p.irq_fault)safe=0;
    marble_gpu_log("marble-smmu: context result passed=%d translation=%d released=%d fault_irq=%u writes=%u stage=%u\n",
        passed,ats,safe,p.irq_fault,p.writes,stage);
    if(!safe)panic("marble-smmu: context cleanup unverified; retain RAM and reset before another run");
    return passed && !p.irq_fault?0:-1;
}

/* GPU SID0/0x400, LPAC SID1/0x401 and ACTLR0x32b are from marble's Mu DT.
 * redwood's tested A660 path records immediate LPAC SQE fetch/reset without
 * SID1. Xiaomi kgsl_iommu maps the same default TTBR0 into user and LPAC.
 * Both engines here share ONLY the five already audited GPU allocations.
 * Outer GMU code owns CX/clocks and the global-fault IRQ throughout this
 * nested bank lease. No other bank or global SMMU configuration is changed. */
static struct probe_io gpu_p;
static ArmSmmu4KIo gpu_io;
static ArmSmmu4KContextConfig gpu_context;
static ArmSmmu4KStreamState gpu_streams;
static unsigned gpu_owned;
static uint32_t gpu_old_actlr;
static unsigned gpu_fault_reported;
static const ArmSmmu4KLiveArena *gpu_live_arena;
static ArmSmmu4KLiveState *gpu_live_state;
static ArmSmmu4KRange gpu_live_ranges[MARBLE_GPU_LIVE_RANGES];
static size_t gpu_live_count;
static ArmSmmu4KRange gpu_pending[MARBLE_GPU_LIVE_RANGES];
static size_t gpu_pending_count;
static int (*gpu_live_quiesce)(void *);
static void *gpu_live_cookie;
static int live_publish(void *cookie,volatile uint64_t *mapping,size_t bytes)
{
    (void)cookie;
    if(!gpu_live_arena || bytes!=4096 || (uintptr_t)mapping<(uintptr_t)gpu_live_arena->mapping ||
        (uintptr_t)mapping-(uintptr_t)gpu_live_arena->mapping>gpu_live_arena->capacity_pages*4096-4096 ||
        ((uintptr_t)mapping&4095))return -1;
    clean_tables((uint64_t *)mapping,1);return 0;
}
int marble_gpu_smmu_live_update(const ArmSmmu4KRange *changes,size_t count,unsigned map)
{
    if(!gpu_owned || !gpu_live_arena || !gpu_live_state || !gpu_live_quiesce ||
        !changes || !count || map>1 || count>MARBLE_GPU_LIVE_RANGES ||
        (map && count>MARBLE_GPU_LIVE_RANGES-gpu_live_count) ||
        !gpu_live_quiesce(gpu_live_cookie) || marble_gpu_smmu_health())return 0;
    ArmSmmu4KLiveVisibility visibility={NULL,live_publish};
    ArmSmmu4KLiveRegion region={gpu_live_arena,gpu_live_state,gpu_live_ranges,gpu_live_count};
    ArmSmmu4KLiveResult result;
    ArmSmmu4KStatus status=ArmSmmu4KLiveUpdateContextBatch(&gpu_io,1,&gpu_context,&region,NULL,0,
        &visibility,changes,count,map,500,&result);
    if(status || !result.complete || result.must_retain_resources || (!map && !result.translation_removed))
        panic("marble GPU live update uncertain (%u, poison %u); retain all BOs until reset",status,gpu_live_state->poisoned);
    if(map) {
        /* Backwards merge; inputs are disjoint and sorted by the toolkit. */
        size_t a=gpu_live_count,b=count,to=a+b;
        while(b) {
            if(a && gpu_live_ranges[a-1].iova>changes[b-1].iova)gpu_live_ranges[--to]=gpu_live_ranges[--a];
            else gpu_live_ranges[--to]=changes[--b];
        }
        gpu_live_count+=count;
    } else {
        size_t to=0,j=0;
        for(size_t i=0;i<gpu_live_count;i++) {
            if(j<count && gpu_live_ranges[i].iova==changes[j].iova) {
                /* Use exact records; no partial ledger removal is permitted. */
                const ArmSmmu4KRange *a=gpu_live_ranges+i,*b=changes+j;
                if(a->physical!=b->physical || a->bytes!=b->bytes || a->permissions!=b->permissions ||
                    a->writeback!=b->writeback || a->shareability!=b->shareability)panic("marble GPU unmap ledger mismatch");
                j++;
            } else gpu_live_ranges[to++]=gpu_live_ranges[i];
        }
        if(j!=count)panic("marble GPU unmap ledger incomplete");
        gpu_live_count=to;
    }
    marble_gpu_log("marble-gpu-live-map: map=%u ranges=%lu leaves=%lu generation=%llu published=%lu tlb_removed=%u PASS=1\n",map,(unsigned long)count,(unsigned long)gpu_live_state->tables.leaf_pages,gpu_live_state->generation,(unsigned long)result.published_pages,result.translation_removed);
    return !marble_gpu_smmu_health();
}
int marble_gpu_smmu_live_ecats(uint64_t iova,uint64_t expected)
{
    if(!gpu_live_arena || !gpu_live_quiesce || !gpu_live_quiesce(gpu_live_cookie))return 0;
    struct tbu_io tp={ml_io_map(0x03de9000,0x1000)};if(!tp.base)return 0;
    ArmSmmu4KIo tbu={&tp,0x1000,tbu_rd32,tbu_rd64,NULL,tbu_wr64,barrier};
    MarbleEcatsResult r;int rc=marble_ecats_sid_access(&tbu,0,iova,3,500,&r);
    if(r.retain || !r.restored)panic("marble GPU live ECATS pending, retain lease");
    int ok=!rc && r.completed && !(r.par&1) && r.physical==expected && !marble_gpu_smmu_health();
    marble_gpu_log("marble-gpu-live-map: ECATS iova=%#llx PA=%#llx expected=%#llx PASS=%u\n",iova,r.physical,expected,ok);
    return ok;
}
int marble_gpu_smmu_health(void)
{
    if(!gpu_owned)return 0;
    uint32_t fsr=0,gfsr=0;
    int ioerr=rd32(&gpu_p,0x21058,&fsr) | rd32(&gpu_p,0x48,&gfsr);
    int bad=ioerr || gpu_p.irq_fault || (fsr&0xc00001feu) || gfsr;
    if(bad && !gpu_fault_reported++) {
        uint64_t far=0;uint32_t syn=0,gsyn=0;
        rd64(&gpu_p,0x21060,&far);rd32(&gpu_p,0x21068,&syn);rd32(&gpu_p,0x54,&gsyn);
        marble_gpu_log("marble-gpu-context: HEALTH_FAIL FSR=%#x FAR=%#llx FSYNR=%#x GFSR=%#x GFSYNR1=%#x irq=%u io=%u\n",fsr,far,syn,gfsr,gsyn,gpu_p.irq_fault,ioerr);
        marble_gpu_log("marble-gpu-context: IRQ_SNAPSHOT FSR=%#x FAR=%#llx FSYNR=%#x SID=%#x\n",gpu_p.irq_fsr,gpu_p.irq_far,gpu_p.irq_syn,gpu_p.irq_sid);
    }
    return bad;
}
/* IOKit serializes each BO's page enumeration and single commit. */
int marble_gpu_smmu_queue_page(uint64_t va,uint64_t pa,unsigned ro)
{
    if(!gpu_owned || !gpu_live_arena || va<0x4000000 || va>=0x20000000 ||
        !pa || ((va|pa)&4095) || pa+4096>(UINT64_C(1)<<36) || ro>1)return 0;
    unsigned permissions=ArmSmmu4KRead|ArmSmmu4KExecute|(ro?0:ArmSmmu4KWrite);
    if(gpu_pending_count) {
        ArmSmmu4KRange *last=&gpu_pending[gpu_pending_count-1];
        if(last->iova+last->bytes!=va)return 0;
        if(last->physical+last->bytes==pa && last->permissions==permissions) {last->bytes+=4096;return 1;}
    }
    if(gpu_pending_count>=MARBLE_GPU_LIVE_RANGES-gpu_live_count)return 0;
    gpu_pending[gpu_pending_count++]=(ArmSmmu4KRange){va,pa,4096,permissions,0,2};return 1;
}
void marble_gpu_smmu_commit_pages(void)
{
    if(!gpu_pending_count)return;
    if(!marble_gpu_smmu_live_update(gpu_pending,gpu_pending_count,1))
        panic("marble GPU BO publication refused; retain wired memory");
    gpu_pending_count=0;
}
void marble_gpu_smmu_unmap_span(uint64_t va,uint64_t bytes)
{
    if(gpu_pending_count || !bytes || ((va|bytes)&4095) || va<0x4000000 || va>=0x20000000 ||
        bytes>0x20000000-va)panic("marble GPU BO unmap arguments");
    size_t count=0;
    for(size_t i=0;i<gpu_live_count;i++) {
        ArmSmmu4KRange *r=gpu_live_ranges+i;
        if(r->iova>=va && r->iova<va+bytes) {
            if(r->bytes>va+bytes-r->iova)panic("marble GPU partial BO record removal");
            gpu_pending[count++]=*r;
        }
    }
    if(count && !marble_gpu_smmu_live_update(gpu_pending,count,0))
        panic("marble GPU BO unmap refused; retain wired memory");
}
static int gpu_smmu_begin(vm_offset_t base,const ArmSmmu4KConfig *lower,
    const ArmSmmu4KResult *result,uint64_t *tables,const ArmSmmu4KRange *ranges,size_t count,size_t capacity)
{
    if(gpu_owned || !base || !lower || !result || !tables || !ranges || !count ||
        count>5 || (gpu_live_arena?capacity!=gpu_live_arena->capacity_pages:capacity!=MARBLE_GPU_TABLE_PAGES))return 0;
    gpu_p=(struct probe_io){base,0,0,0,17,1,1};
    gpu_io=(ArmSmmu4KIo){&gpu_p,0x40000,rd32,rd64,wr32,wr64,barrier};
    gpu_context=(ArmSmmu4KContextConfig){lower,NULL,result,NULL,0x4c017e18,0x5511,MARBLE_GPU_ASID,1,0,0};
    ArmSmmu4KStream routes[4]={{2,0},{3,0x400},{4,1},{5,0x401}};ArmSmmu4KStreamResult sr;
    if(ArmSmmu4KCheckFaultedStreams(&gpu_io,routes,4,1,&sr))return 0;
    for(unsigned bank=1;bank<21;bank++) {
        uint32_t sctlr;uint64_t ttbr;
        if(rd32(&gpu_p,0x20000+bank*4096,&sctlr) || (sctlr&1) ||
            rd64(&gpu_p,0x20020+bank*4096,&ttbr) || (ttbr>>48)==MARBLE_GPU_ASID)return 0;
    }
    uint32_t fsr;
    if(rd32(&gpu_p,0x21058,&fsr) || (fsr&0xc00001feu) || rd32(&gpu_p,0x21004,&gpu_old_actlr))return 0;
    unsigned char visited[260];struct table_io ti={lower,tables,capacity};
    ArmSmmu4KAuditRegion region={ranges,count,capacity,capacity*512,visited,sizeof(visited)};
    ArmSmmu4KAuditConfig audit={{&ti,read_descriptor},&region,NULL};ArmSmmu4KAuditResult ar;
    if(gpu_live_arena) {
        ArmSmmu4KLiveResult live;
        if(ArmSmmu4KLiveAudit(gpu_live_arena,gpu_live_state,NULL,0,&live))return 0;
        gpu_context.lower_tables=&gpu_live_state->tables;
    } else if(ArmSmmu4KAuditDeclaredContext(&gpu_context,&audit,&ar))return 0;
    if(gic_v3_spi_attach(679u+32,false,fault_irq,&gpu_p)!=KERN_SUCCESS)return 0;
    clean_tables(tables,capacity);gpu_owned=1;
    uint32_t actlr;
    if(wr32(&gpu_p,0x21004,0x32b) || rd32(&gpu_p,0x21004,&actlr) || actlr!=0x32b)
        panic("marble GPU ACTLR publication uncertain; retain lease, reset required");
    ArmSmmu4KProgramResult pr;
    ArmSmmu4KStatus status=ArmSmmu4KProgramDisabledContext(&gpu_io,1,&gpu_context,500,&pr);
    marble_gpu_log("marble-gpu-context: program status=%u phase=%u enabled=%u retain=%u bank=1 asid=%#x\n",status,pr.phase,pr.enabled,pr.must_retain_tables,MARBLE_GPU_ASID);
    if(status)panic("marble GPU context publication failed; retain lease, reset required");
    if(gpu_live_arena?ArmSmmu4KObserveOwnedContext(&gpu_io,1,&gpu_context,&pr):
        ArmSmmu4KObserveAuditedContext(&gpu_io,1,&gpu_context,&audit,&ar))
        panic("marble GPU active physical-table audit failed; retain lease");
    gpu_streams=(ArmSmmu4KStreamState){0};
    status=ArmSmmu4KAttachStreams(&gpu_io,&gpu_streams,routes,4,1,&gpu_context,&sr);
    marble_gpu_log("marble-gpu-context: attach status=%u streams=%u bank=1 SID0=0 SID1=0x400 LPAC0=1 LPAC1=0x401 tables=%lu leaves=%lu\n",status,sr.streams_verified,(unsigned long)result->table_pages,(unsigned long)(gpu_live_arena?0:ar.lower.leaf_pages));
    if(status)panic("marble GPU stream publication failed; retain lease, reset required");
    if(gpu_live_arena && !marble_gpu_smmu_live_update(ranges,count,1))return 0;
    struct tbu_io tp={ml_io_map(0x03de9000,0x1000)};if(!tp.base)return 0;
    ArmSmmu4KIo tbu={&tp,0x1000,tbu_rd32,tbu_rd64,NULL,tbu_wr64,barrier};
    unsigned passed=1;
    for(size_t i=0;i<count;i++) {
        MarbleEcatsResult er;unsigned access=(ranges[i].permissions&ArmSmmu4KExecute)?5:3;
        int rc=marble_ecats_sid_access(&tbu,0,ranges[i].iova,access,500,&er);
        unsigned ok=!rc && er.completed && er.restored && !er.retain && !(er.par&1) &&
            er.physical==ranges[i].physical && !marble_gpu_smmu_health();
        marble_gpu_log("marble-gpu-context: ECATS index=%lu sid=0 iova=%#llx PA=%#llx expected=%#llx par=%#llx status=%d restored=%u retain=%u PASS=%u\n",(unsigned long)i,ranges[i].iova,er.physical,ranges[i].physical,er.par,rc,er.restored,er.retain,ok);
        if(er.retain || !er.restored)panic("marble GPU ECATS pending; retain lease, reset required");
        passed&=ok;if(!ok)break;
    }
    marble_gpu_log("marble-gpu-context: ready PASS=%u bank=1 asid=%#x routes=4 ranges=%lu writes=%u\n",passed,MARBLE_GPU_ASID,(unsigned long)count,gpu_p.writes);
    return passed;
}
int marble_gpu_smmu_begin(vm_offset_t base,const ArmSmmu4KConfig *lower,
    const ArmSmmu4KResult *result,uint64_t *tables,const ArmSmmu4KRange *ranges,size_t count,size_t capacity)
{
    if(gpu_live_arena)return 0;
    return gpu_smmu_begin(base,lower,result,tables,ranges,count,capacity);
}
int marble_gpu_smmu_live_begin(vm_offset_t base,const ArmSmmu4KLiveArena *arena,ArmSmmu4KLiveState *state,
    const ArmSmmu4KRange *ranges,size_t count,int (*quiesce)(void *),void *cookie)
{
    if(gpu_owned || gpu_live_arena || !arena || !state || !quiesce || !state->prepared || state->poisoned ||
        state->tables.leaf_pages || arena->capacity_pages>260)return 0;
    gpu_live_arena=arena;gpu_live_state=state;gpu_live_quiesce=quiesce;gpu_live_cookie=cookie;
    return gpu_smmu_begin(base,&arena->config,&state->tables,(uint64_t *)arena->mapping,ranges,count,arena->capacity_pages);
}
int marble_gpu_smmu_end(vm_offset_t gmu,vm_offset_t cc)
{
    if(!gpu_owned){gpu_live_arena=NULL;gpu_live_state=NULL;gpu_live_count=0;return 1;}
    uint32_t reset,gx;
    __asm__ volatile("ldr %w0, [%1]":"=r"(reset):"r"(gmu+0x16000):"memory");
    __asm__ volatile("ldr %w0, [%1]":"=r"(gx):"r"(cc+0x905c):"memory");
    if(reset!=1 || (gx&0x80000000u) || marble_gpu_smmu_health())return 0;
    ArmSmmu4KStreamResult sr;ArmSmmu4KProgramResult pr;
    if(ArmSmmu4KFaultStreams(&gpu_io,&gpu_streams,&sr))return 0;
    for(unsigned slot=2;slot<6;slot++) {
        uint32_t v;
        if(wr32(&gpu_p,0x800+slot*4,0) || rd32(&gpu_p,0x800+slot*4,&v) || v)return 0;
    }
    if(ArmSmmu4KDisableOwnedContext(&gpu_io,1,&gpu_context,500,&pr) ||
        !pr.disabled_verified || !pr.tlb_synchronized || pr.must_retain_tables)return 0;
    if(wr64(&gpu_p,0x21020,0) || wr64(&gpu_p,0x21028,0) || wr32(&gpu_p,0x21004,gpu_old_actlr))return 0;
    uint64_t lo,hi;uint32_t sctlr,actlr;
    if(rd64(&gpu_p,0x21020,&lo) || rd64(&gpu_p,0x21028,&hi) || lo || hi ||
        rd32(&gpu_p,0x21000,&sctlr) || sctlr || rd32(&gpu_p,0x21004,&actlr) || actlr!=gpu_old_actlr ||
        ArmSmmu4KCheckFaultedStreams(&gpu_io,gpu_streams.streams,4,1,&sr))return 0;
    gic_v3_spi_detach(679u+32);
    marble_gpu_log("marble-gpu-context: stopped PASS=1 reset=%u gx=%#x bank=1 roots_zero=1 routes_faulted=4 tlb_sync=1 irq_fault=%u writes=%u\n",reset,gx,gpu_p.irq_fault,gpu_p.writes);
    gpu_owned=0;gpu_live_arena=NULL;gpu_live_state=NULL;gpu_live_count=0;return 1;
}
