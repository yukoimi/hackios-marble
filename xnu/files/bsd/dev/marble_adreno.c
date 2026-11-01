/*
 * Adreno725/Gen7_4_0 bring-up for marble. Reuses UniXNU's AdrenoFirmware
 * planner and ArmSmmu4K inactive table builder; never runs A740 init tables.
 * marble_gpu_probe=1: board/firmware/table preflight, no device accesses.
 * marble_gpu_probe=2: additionally read the GPUCC status left by firmware.
 * marble_gpu_probe=3: bounded CX/local-clock transaction with restoration.
 * marble_gpu_probe=4: also audit GR1/CB registers and GMU reset.
 * marble_gpu_probe=5: program/audit/disable CB0; ATS only when IDR0 permits.
 * marble_gpu_probe=6: read-only gfx0 TBU/TCU audit.
 * marble_gpu_probe=7: exact SID5/CB0 lease, ECATS translation, quarantine/release.
 * marble_gpu_probe=8: full firmware RAM reservations and privileged ECATS.
 * marble_gpu_probe=9: additionally snapshot/stability-check both TCM banks.
 * marble_gpu_probe=10: load/readback/restore only the three TCM payloads.
 * marble_gpu_probe=11: log/HFI allocation, stopped KGSL queue image and ECATS.
 * marble_gpu_probe=12: additionally read clock configuration and command DB.
 * marble_gpu_probe=13: CPU-only candidate Gen7 performance/bandwidth encoding.
 * Stages14..16: restored GMU/HUB clocks, RPMh reads and vote adequacy.
 * Stage17: GMU FW_INIT/HFI, GX OOB, GPU/LPAC context and CP DMA.
 * The current experimental candidate additionally tests PAS13 ZAP lifecycle.
 * No stage creates a GPU service or enables iOS acceleration.
 */
#include <libkern/libkern.h>
#include <kern/clock.h>
#include <kern/thread.h>
#include <libkern/crypto/sha2.h>
#include <pexpert/pexpert.h>
#include <pexpert/device_tree.h>
#include <sys/systm.h>
#include <mach/mach_types.h>
#include "marble_gpu/marble_gpu_plan.h"
#include "marble_gpu/marble_gpu_power.h"
#include "marble_gpu/upstream/ArmSmmu4KContext.h"
#include "marble_gpu/marble_smmu.h"
#include "marble_gpu/marble_gmu_memory.h"
#include "marble_gpu/marble_gmu_tcm.h"
#include "marble_gpu/marble_gmu_resources.h"
#include "marble_gpu/marble_gmu_preflight.h"
#include "marble_gpu/marble_gmu_votes.h"
#include "marble_gpu/marble_gmu_clocks.h"
#include "marble_gpu/marble_gmu_rpmh.h"
#include "marble_gpu/marble_gmu_dependencies.h"
#include "marble_gpu/marble_gmu_boot.h"
#include "marble_gpu/marble_gmu_hfi.h"
#include "marble_gpu/marble_gmu_sequencers.h"
#include "marble_gpu/marble_gmu_gx.h"
#include "marble_gpu/marble_gpu_context.h"
#include "marble_gpu/marble_gpu_cp.h"
#include "marble_gpu/marble_gpu_zap.h"
#include "marble_gpu/marble_gpu_secure.h"
#include "marble_gpu/marble_gpu_blit.h"
#include "marble_gpu/marble_gpu_service.h"
#include "marble_gpu/marble_gpu_pending.h"
#include "marble_gpu/marble_gmu_diagnostic.h"
#include "marble_gpu/upstream/AdrenoHfi.h"
#include "marble_gpu/upstream/AdrenoHfiVotes.h"

extern vm_offset_t ml_io_map(vm_offset_t,vm_size_t);
extern vm_offset_t ml_io_map_wcomb(vm_offset_t,vm_size_t);
extern uint64_t kvtophys(vm_offset_t);
extern const uint8_t *redwood_image_file(const char *,const char *,uint64_t *,int *);
extern void loader_log_putc(char);
extern kern_return_t thread_terminate(thread_t);

/* A single worker retains the nested, audited hardware leases. Only it may
 * touch the GPU. Stop is published before waiting; panic cannot wait for it.
 * No runtime BOs or service are exposed by this first lifecycle test. */
static unsigned marble_present, runtime_mode, runtime_inflight, runtime_stop_requested;
static unsigned runtime_active, runtime_draws, runtime_wraps;
static unsigned runtime_failed;
static uint64_t runtime_cc;
static unsigned runtime_gpu_started,runtime_position;
static uint32_t runtime_fence;
static unsigned service_started,service_accepting,service_submissions;
static void *service_cookie;
static MarbleGpuPending service_pending;
static int service_complete(unsigned wait);
void marble_gpu_prepare(void);
extern void RedwoodGPUStart(void);
extern void RedwoodGPUStop(void);
extern bool marble_ring_adopt(void *,void *,void *,uint32_t,uint32_t,
    int (*)(void *,uint32_t,uint32_t),int (*)(void *,uint32_t,uint32_t *),int (*)(void *));
extern bool marble_ring_submit(const uint64_t *,const uint32_t *,uint32_t,uint32_t *,uint32_t *);
extern bool marble_ring_retire(uint32_t);
static uint64_t runtime_tables[260*512] __attribute__((aligned(16384)));
static uint32_t runtime_alias_scratch[MARBLE_GPU_LIVE_RANGES];
static ArmSmmu4KLiveArena runtime_arena;
static ArmSmmu4KLiveState runtime_table_state;
static uint32_t runtime_target[4096] __attribute__((aligned(16384)));
bool marble_adreno_stop(void);
bool marble_adreno_quiesce(void);
bool marble_adreno_quiesce(void)
{
    if(!__atomic_load_n(&marble_present,__ATOMIC_ACQUIRE))return false;
    if(__atomic_load_n(&runtime_inflight,__ATOMIC_ACQUIRE)) {
        const char *s="marble-gpu-runtime: unverified live leases; SYSTEM_RESET, no loader RAM reuse\n";
        while(*s)loader_log_putc(*s++);
        register uint64_t x0 __asm__("x0")=0x84000009; /* PSCI SYSTEM_RESET */
        __asm__ volatile("smc #0":"+r"(x0)::"x1","x2","x3","x4","x5","x6","x7","x8","x9","x10","x11","x12","x13","x14","x15","x16","x17","memory");
        /* A rejected reset must not allow a jump into reusable loader RAM. */
        for(;;)__asm__ volatile("wfe");
    }
    return true;
}
bool marble_adreno_stop(void)
{
    if(!__atomic_load_n(&marble_present,__ATOMIC_ACQUIRE))return false;
    if(__atomic_load_n(&service_started,__ATOMIC_ACQUIRE)){RedwoodGPUStop();return true;}
    __atomic_store_n(&runtime_stop_requested,1,__ATOMIC_RELEASE);
    for(unsigned n=0;n<2000 && __atomic_load_n(&runtime_inflight,__ATOMIC_ACQUIRE);n++)
        delay_for_interval(10,NSEC_PER_MSEC);
    if(__atomic_load_n(&runtime_inflight,__ATOMIC_ACQUIRE))marble_adreno_quiesce();
    return true;
}
bool marble_gpu_board(void){return !!__atomic_load_n(&marble_present,__ATOMIC_ACQUIRE);}
bool marble_gpu_ready(void)
{return __atomic_load_n(&service_started,__ATOMIC_ACQUIRE) && __atomic_load_n(&runtime_inflight,__ATOMIC_ACQUIRE);}
void marble_gpu_stop_locked(void)
{
    service_accepting=0;
    /* gGPULock prevents publication/free/map while the last batch drains. */
    service_complete(1);
    __atomic_store_n(&runtime_stop_requested,1,__ATOMIC_RELEASE);
    for(unsigned n=0;n<2000 && __atomic_load_n(&runtime_inflight,__ATOMIC_ACQUIRE);n++)
        delay_for_interval(10,NSEC_PER_MSEC);
    if(__atomic_load_n(&runtime_inflight,__ATOMIC_ACQUIRE))marble_adreno_quiesce();
}
bool marble_gpu_parameter(uint32_t which,uint64_t *value)
{
    if(!value)return false;
    switch(which) {
    case 1:case 6:case 8:case 9:case 10:case 17:*value=0;return true;
    case 2:*value=2*1024*1024;return true;
    case 3:*value=0x07030002;return true;
    case 4:*value=220000000;return true;
    case 5:*value=mach_absolute_time();return true;
    case 7:*value=1;return true;
    case 16:*value=16;return true;
    case 18:*value=6;return true;
    case 19:*value=1;return true;
    default:return false;
    }
}
bool marble_gpu_map(uint64_t address,uint64_t pa,bool ro)
{return service_accepting && marble_gpu_smmu_queue_page(address,pa,ro);}
void marble_gpu_mapped(void){marble_gpu_smmu_commit_pages();}
void marble_gpu_unmap(uint64_t address,uint64_t bytes){marble_gpu_smmu_unmap_span(address,bytes);}
bool marble_gpu_done(uint32_t fence)
{
    if(!fence || (int32_t)(runtime_fence-fence)>=0)return true;
    service_complete(0);
    return (int32_t)(runtime_fence-fence)>=0;
}
uint32_t marble_gpu_last(void){return service_pending.active?service_pending.fence:runtime_fence;}
/* All callers hold gGPULock. Drain before touching cache lines a GPU may write. */
void marble_gpu_prepare(void){if(service_pending.active)service_complete(1);}
/* Only for a complete BO range whose latest use (including IOSurface aliases)
 * is supplied under gGPULock. Unrelated queued work keeps its ownership. */
bool marble_gpu_prepare_buffer(uint32_t fence)
{
    service_complete(0);
    if(fence && (int32_t)(runtime_fence-fence)<0)service_complete(1);
    if(fence && (int32_t)(runtime_fence-fence)<0)
        panic("marble GPU buffer cache dependency not completed");
    return !!service_pending.active;
}

void marble_gpu_log(const char *format,...) __printflike(1,2);
void marble_gpu_log(const char *format,...)
{
    char line[384];va_list args;va_start(args,format);
    vsnprintf(line,sizeof(line),format,args);va_end(args);
    for(const char *p=line;*p;p++)loader_log_putc(*p);
    printf("%s",line);
}
struct firmware {const char *name,*sha256;size_t bytes;const uint8_t *data;};
static struct firmware files[] = {
    {"gmu_gen70000.bin","b2d3f0e2a98fd6259ab9ffef16df63265542e6cc9a970d53d476e823d432042a",57272,NULL},
    {"a730_sqe.fw","e67d1829f57fc8326d806234b932dc3b78e28a9940df34c70752f17d5f38b7aa",74676,NULL},
    {"a730_zap.mdt","b957876634e6a9eab17ae39912ea3b4cb5ba811d0720b04ba0590e118c3cd46a",4044,NULL},
    {"a730_zap.b00","9cd65a08fa2ce72c05a52aad95951f749f52fc310613a2857e270b463a641a7f",148,NULL},
    {"a730_zap.b01","1fc98f97cf354b9704b20d0848f7b2739638539a57d88ecf5cc71ca1c5c4e6b9",2236,NULL},
    {"a730_zap.b02","41a11d0a7c9a6dd523bc1da3fdbeb887f04a9295072391fcdd4b8b71f713fedc",3896,NULL}
};
static uint64_t offline_tables[MARBLE_GMU_TABLE_CAPACITY*512] __attribute__((aligned(16384)));
static uint8_t offline_page[4096] __attribute__((aligned(4096)));
static uint8_t gmu_ram[MARBLE_GMU_BACKING_BYTES] __attribute__((aligned(16384)));
static AdrenoFirmwareBlock firmware_blocks[32];
static size_t firmware_block_count;
static MarbleGmuMemoryPlan gmu_plan;
static uint8_t gmu_ram_sha[32];
static uint32_t gmu_log[MARBLE_GMU_LOG_BYTES/4] __attribute__((aligned(16384)));
static uint32_t gmu_hfi[MARBLE_GMU_HFI_BYTES/4] __attribute__((aligned(16384)));
static AdrenoHfi hfi_transport;
static MarbleGmuResourcePlan resource_plan;
static unsigned hfi_notify_attempts;
static uint64_t gpu_tables[MARBLE_GPU_TABLE_PAGES*512] __attribute__((aligned(16384)));
static uint8_t gpu_ram[MARBLE_GPU_RAM_BYTES] __attribute__((aligned(16384)));
static ArmSmmu4KRange gpu_ranges[5];
static ArmSmmu4KConfig gpu_config;
static ArmSmmu4KResult gpu_table_result;
static vm_offset_t gpu_uncached;
static uint8_t resource_sha[32];
static MarbleTcmPlan tcm_plan;
static uint32_t tcm_snapshot[MARBLE_TCM_WORDS];
static ArmSmmu4KRange offline_ranges[MARBLE_GMU_RESOURCE_RANGE_CAPACITY];
static size_t offline_range_count,offline_table_capacity;
static ArmSmmu4KConfig offline_config;
static ArmSmmu4KResult offline_result;
static unsigned probe_stage;
static uint8_t cmd_db_snapshot[MARBLE_CMD_DB_BYTES];
static MarbleGmuVotePlan vote_plan;
static MarbleRpmhResult rpmh_arc_snapshot;
extern int marble_smmu_probe(vm_offset_t,unsigned,const ArmSmmu4KConfig *,const ArmSmmu4KResult *,
    uint64_t *,const ArmSmmu4KRange *,size_t,size_t,int (*)(void *),void *);

static int firmware_preflight(void)
{
    for(unsigned i=0;i<sizeof(files)/sizeof(files[0]);i++) {
        struct firmware *f=files+i;uint64_t size=0;uint8_t digest[32];char hex[65];SHA256_CTX ctx;
        f->data=redwood_image_file("fw",f->name,&size,NULL);
        if(!f->data || size!=f->bytes) {
            marble_gpu_log("marble-gpu: missing/wrong-size RAM firmware %s: %llu\n",f->name,size);return 0;
        }
        SHA256_Init(&ctx);SHA256_Update(&ctx,f->data,(size_t)size);SHA256_Final(digest,&ctx);
        for(unsigned j=0;j<32;j++) {hex[j*2]="0123456789abcdef"[digest[j]>>4];hex[j*2+1]="0123456789abcdef"[digest[j]&15];}
        hex[64]=0;
        if(strcmp(hex,f->sha256)) {marble_gpu_log("marble-gpu: hash mismatch %s\n",f->name);return 0;}
        marble_gpu_log("marble-gpu: firmware %s %lu bytes SHA256 OK\n",f->name,(unsigned long)size);
    }
    AdrenoFirmwareBlock *blocks=firmware_blocks;size_t count=0;unsigned payloads=0;
    AdrenoFirmwareResult r=AdrenoFirmwarePlanGmu(files[0].data,files[0].bytes,
        marble_gmu_regions,sizeof(marble_gmu_regions)/sizeof(marble_gmu_regions[0]),blocks,32,&count);
    if(r!=AdrenoFirmwareSuccess) {marble_gpu_log("marble-gpu: Gen7 GMU plan refused %u\n",r);return 0;}
    firmware_block_count=count;
    for(size_t i=0;i<count;i++) {
        if(blocks[i].bytes)payloads++;
        marble_gpu_log("marble-gpu: GMU block %lu addr=%#x bytes=%#x type=%#x value=%#x region=%u\n",
            (unsigned long)i,blocks[i].address,blocks[i].bytes,blocks[i].type,blocks[i].value,blocks[i].region_identifier);
    }
    const uint8_t *parts[]={files[3].data,files[4].data,files[5].data};
    size_t lengths[]={files[3].bytes,files[4].bytes,files[5].bytes};
    if(!marble_gpu_zap_structure(files[2].data,files[2].bytes,parts,lengths)) {
        marble_gpu_log("marble-gpu: split ZAP structure refused\n");return 0;
    }
    marble_gpu_log("marble-gpu: GMU plan %lu blocks/%u payloads; ZAP structure OK, not authenticated\n",(unsigned long)count,payloads);
    return 1;
}

static int hfi_notify_forbidden(void *cookie)
{(void)cookie;hfi_notify_attempts++;return -1;}
static int gpu_inactive_memory(void)
{
    uint64_t pa=kvtophys((vm_offset_t)gpu_ram),tp=kvtophys((vm_offset_t)gpu_tables);
    if(!pa || !tp || (pa&0x3fff) || (tp&0x3fff) || pa+sizeof(gpu_ram)>(UINT64_C(1)<<36))return 0;
    for(size_t i=0;i<sizeof(gpu_ram);i+=4096)if(kvtophys((vm_offset_t)gpu_ram+i)!=pa+i)return 0;
    for(size_t i=0;i<sizeof(gpu_tables);i+=4096)if(kvtophys((vm_offset_t)gpu_tables+i)!=tp+i)return 0;
    memset(gpu_ram,0,sizeof(gpu_ram));
    /* Pinned Xiaomi adreno_get_firmware skips the 4-byte SQE file header. */
    if(files[1].bytes<8 || files[1].bytes-4>0x20000)return 0;
    memcpy(gpu_ram,files[1].data+4,files[1].bytes-4);
    gpu_ranges[0]=(ArmSmmu4KRange){0x100000,pa,0x20000,ArmSmmu4KRead|ArmSmmu4KExecute,0,2};
    for(unsigned i=1;i<5;i++)gpu_ranges[i]=(ArmSmmu4KRange){0x100000+i*0x100000,pa+0x20000+(i-1)*0x4000,0x4000,ArmSmmu4KRead|ArmSmmu4KWrite,0,2};
    gpu_config=(ArmSmmu4KConfig){32,36,tp,sizeof(gpu_ram)/4096};
    if(ArmSmmu4KBuild(&gpu_config,gpu_ranges,5,gpu_tables,MARBLE_GPU_TABLE_PAGES,&gpu_table_result))return 0;
    /* CPU gives this allocation to its uncached view before any device can
     * access it; no cached alias is accessed while the GPU bank is active. */
    uint64_t ctr;__asm__ volatile("mrs %0, ctr_el0":"=r"(ctr));size_t line=(size_t)4<<((ctr>>16)&15);
    for(vm_offset_t va=(vm_offset_t)gpu_ram;va<(vm_offset_t)gpu_ram+sizeof(gpu_ram);va+=line)
        __asm__ volatile("dc civac, %0"::"r"(va):"memory");
    __asm__ volatile("dsb sy":::"memory");
    gpu_uncached=ml_io_map_wcomb(pa,sizeof(gpu_ram));
    marble_gpu_log("marble-gpu-context: inactive PASS=%u PA=%#llx table_PA=%#llx pages=%lu leaves=%lu SQE_bytes=%lu SQE_skip=4\n",!!gpu_uncached,pa,tp,(unsigned long)gpu_table_result.table_pages,(unsigned long)gpu_table_result.leaf_pages,(unsigned long)files[1].bytes-4);
    return !!gpu_uncached;
}
static void resources_digest(uint8_t *digest)
{
    SHA256_CTX ctx;SHA256_Init(&ctx);SHA256_Update(&ctx,gmu_log,sizeof(gmu_log));
    SHA256_Update(&ctx,gmu_hfi,sizeof(gmu_hfi));SHA256_Final(digest,&ctx);
}
static int inactive_tables(unsigned stage)
{
    uint64_t pa=kvtophys((vm_offset_t)offline_tables),data=kvtophys((vm_offset_t)offline_page);
    if(!pa || !data || (pa&4095) || (data&4095))return 0;
    offline_table_capacity=stage>=8?MARBLE_GMU_TABLE_CAPACITY:4;
    for(size_t i=0;i<offline_table_capacity;i++)if(kvtophys((vm_offset_t)offline_tables+i*4096)!=pa+i*4096)return 0;
    /* Actual marble IDR2=0x5511 reports OAS=36; the owned XNU RAM used
     * here is below 2^36. Do not inherit sheng's wider output profile. */
    ArmSmmu4KConfig config={32,36,pa,4};
    offline_ranges[0]=(ArmSmmu4KRange){0x60000000,data,4096,ArmSmmu4KRead|ArmSmmu4KWrite,1,3};
    offline_range_count=1;
    if(stage>=8) {
        data=kvtophys((vm_offset_t)gmu_ram);
        if(!marble_gmu_memory_plan(firmware_blocks,firmware_block_count,data,sizeof(gmu_ram),&gmu_plan) ||
            gmu_plan.count!=6 || gmu_plan.pages!=51 || gmu_plan.ram_payloads!=1 || gmu_plan.tcm_payloads!=3)return 0;
        for(size_t i=0;i<sizeof(gmu_ram);i+=4096)
            if(kvtophys((vm_offset_t)gmu_ram+i)!=data+i)return 0;
        memset(gmu_ram,0,sizeof(gmu_ram));
        memcpy(offline_ranges,gmu_plan.ranges,gmu_plan.count*sizeof(offline_ranges[0]));
        offline_range_count=gmu_plan.count;config.leaf_budget=gmu_plan.pages;
        /* Copy only system-RAM payloads. TCM and reset registers untouched. */
        for(size_t i=0;i<firmware_block_count;i++) {
            const AdrenoFirmwareBlock *b=firmware_blocks+i;if(!b->bytes || b->region_identifier==0 || b->region_identifier==2)continue;
            unsigned copied=0;
            for(size_t j=0;j<offline_range_count;j++) {
                const ArmSmmu4KRange *r=offline_ranges+j;
                if(b->address>=r->iova && b->bytes<=r->bytes && b->address-r->iova<=r->bytes-b->bytes) {
                    size_t offset=(size_t)(r->physical-data+b->address-r->iova);
                    memcpy(gmu_ram+offset,files[0].data+b->payload_offset,b->bytes);copied++;
                }
            }
            if(copied!=1)return 0;
        }
        SHA256_CTX ctx;SHA256_Init(&ctx);SHA256_Update(&ctx,gmu_ram,sizeof(gmu_ram));SHA256_Final(gmu_ram_sha,&ctx);
        uint64_t ctr;__asm__ volatile("mrs %0, ctr_el0":"=r"(ctr));
        size_t line=(size_t)4<<((ctr>>16)&15);
        for(vm_offset_t va=(vm_offset_t)gmu_ram;va<(vm_offset_t)gmu_ram+sizeof(gmu_ram);va+=line)
            __asm__ volatile("dc cvac, %0"::"r"(va):"memory");
        __asm__ volatile("dsb sy":::"memory");
        for(size_t i=0;i<offline_range_count;i++) {
            const ArmSmmu4KRange *r=offline_ranges+i;
            marble_gpu_log("marble-gmu-memory: range=%lu iova=%#llx PA=%#llx bytes=%#llx permissions=%#x\n",
                (unsigned long)i,r->iova,r->physical,r->bytes,r->permissions);
        }
        marble_gpu_log("marble-gmu-memory: " MARBLE_GMU_MEMORY_ABI " ranges=%lu pages=%lu RAM_payloads=%lu TCM_payloads_deferred=%lu; GMU reset\n",
            (unsigned long)gmu_plan.count,(unsigned long)gmu_plan.pages,(unsigned long)gmu_plan.ram_payloads,(unsigned long)gmu_plan.tcm_payloads);
        if(stage>=11) {
            uint64_t log_pa=kvtophys((vm_offset_t)gmu_log),hfi_pa=kvtophys((vm_offset_t)gmu_hfi);
            if(!marble_gmu_resources_plan(&gmu_plan,log_pa,hfi_pa,&resource_plan))return 0;
            for(unsigned i=0;i<0x4000;i+=4096)
                if(kvtophys((vm_offset_t)gmu_log+i)!=log_pa+i || kvtophys((vm_offset_t)gmu_hfi+i)!=hfi_pa+i)return 0;
            memset(gmu_log,0,sizeof(gmu_log));
            int status=AdrenoHfiInitializeStoppedProfile(&hfi_transport,gmu_hfi,sizeof(gmu_hfi),
                resource_plan.hfi_iova,NULL,hfi_notify_forbidden,AdrenoHfiProfileMarbleGen7);
            int valid=!status && marble_hfi_image_valid(gmu_hfi,sizeof(gmu_hfi),resource_plan.hfi_iova);
            marble_gpu_log("marble-gmu-resources: HFI init_status=%d layout_ok=%d table_words=210 queue_offset=0x350 debug_priority=40 notify=%u\n",
                status,valid,hfi_notify_attempts);
            if(!valid || hfi_notify_attempts)return 0;
            memcpy(offline_ranges,resource_plan.ranges,resource_plan.count*sizeof(offline_ranges[0]));
            offline_range_count=resource_plan.count;config.leaf_budget=resource_plan.pages;
            resources_digest(resource_sha);
            for(unsigned j=0;j<2;j++) {
                vm_offset_t start=(vm_offset_t)(j?gmu_hfi:gmu_log);
                for(vm_offset_t va=start;va<start+0x4000;va+=line)
                    __asm__ volatile("dc cvac, %0"::"r"(va):"memory");
                const ArmSmmu4KRange *r=offline_ranges+6+j;
                marble_gpu_log("marble-gmu-resources: range=%u iova=%#llx PA=%#llx bytes=%#llx permissions=%#x\n",
                    6+j,r->iova,r->physical,r->bytes,r->permissions);
            }
            __asm__ volatile("dsb sy":::"memory");
            marble_gpu_log("marble-gmu-resources: " MARBLE_GMU_RESOURCES_ABI " ranges=%lu pages=%lu log_iova=%#x hfi_iova=%#x GENERAL8_planned=%#x; registers NOT written\n",
                (unsigned long)resource_plan.count,(unsigned long)resource_plan.pages,resource_plan.log_iova,
                resource_plan.hfi_iova,resource_plan.log_register);
        }
    }
    ArmSmmu4KResult result;
    ArmSmmu4KStatus status=ArmSmmu4KBuild(&config,offline_ranges,offline_range_count,offline_tables,offline_table_capacity,&result);
    if(status!=ArmSmmu4KSuccess) {marble_gpu_log("marble-gpu: inactive 4K table build refused %u\n",status);return 0;}
    offline_config=config;offline_result=result;
    marble_gpu_log("marble-gpu: inactive 4K tables %lu pages/%lu leaves root=%#llx; not activated\n",
        (unsigned long)result.table_pages,(unsigned long)result.leaf_pages,result.root_physical);
    return 1;
}

static uint32_t read_status(vm_offset_t base,unsigned offset,const char *name)
{
    uint32_t value;
    marble_gpu_log("marble-gpu: reading %s offset=%#x\n",name,offset);
    __asm__ volatile("ldr %w0, [%1]":"=r"(value):"r"(base+offset):"memory");
    marble_gpu_log("marble-gpu: %s=%#x\n",name,value);
    return value;
}

static vm_offset_t clock_preflight(void)
{
    /* Pinned Xiaomi gcc-waipio.c, cape DT: CFG_AHB is 0x81004, not
     * sheng/SM8550's 0x71004. GCC itself is at 0x00100000.
     * HWCG (bit1) can report CLK_OFF while idle; require enable bit0. */
    vm_offset_t gcc=ml_io_map(0x00181000,0x1000);
    if(!gcc) {marble_gpu_log("marble-gpu: GCC map failed\n");return 0;}
    uint32_t cfg=read_status(gcc,4,"GCC_GPU_CFG_AHB_CBCR");
    read_status(gcc,0x10,"GCC_GPU_MEMNOC_GFX_CBCR");
    read_status(gcc,0x18,"GCC_GPU_SNOC_DVM_GFX_CBCR");
    if(!(cfg&1)) {
        marble_gpu_log("marble-gpu: GPUCC SKIPPED: GCC CFG_AHB enable bit is clear\n");return 0;
    }
    vm_offset_t cc=ml_io_map(0x03d90000,0xa000);
    if(!cc) {marble_gpu_log("marble-gpu: GPUCC map failed\n");return 0;}
    static const struct {const char *name;unsigned offset;} registers[]={
        {"GPUCC_CX_GDSCR",0x9108},{"GPUCC_GX_GDSCR",0x905c},{"GPUCC_AHB_CBCR",0x911c},
        {"GPUCC_CX_GMU_CBCR",0x913c},{"GPUCC_HUB_CX_INT_CBCR",0x9148},
        {"GPUCC_HLOS1_SMMU_CBCR",0x7000}};
    for(unsigned i=0;i<sizeof(registers)/sizeof(registers[0]);i++)
        read_status(cc,registers[i].offset,registers[i].name);
    marble_gpu_log("marble-gpu: GPUCC observation COMPLETE\n");
    return cc;
}

static uint32_t power_read(void *cookie,unsigned offset)
{
    uint32_t value;vm_offset_t address=(vm_offset_t)cookie+offset;
    __asm__ volatile("ldr %w0, [%1]":"=r"(value):"r"(address):"memory");return value;
}
static void power_write(void *cookie,unsigned offset,uint32_t value)
{
    vm_offset_t address=(vm_offset_t)cookie+offset;
    marble_gpu_log("marble-gpu: CX transaction write offset=%#x value=%#x\n",offset,value);
    __asm__ volatile("str %w0, [%1]\n\tdsb sy"::"r"(value),"r"(address):"memory");
}
static void power_delay(void *cookie,unsigned us)
{(void)cookie;delay(us);}
struct tcm_device {vm_offset_t gmu,cc;};
static unsigned boot_tcm_lease;
static unsigned boot_gx_lease;
static MarbleGxResult gx_result;
static uint32_t boot_dma_values[4];
static const unsigned gmu_diagnostic_mode=0; /* 0 vendor firmware, 1 external fetch diagnostic */
static const unsigned gx_manual_cleanup_test=0; /* fallback verified before CP work */
static int tcm_read(void *cookie,unsigned offset,uint32_t *value)
{
    struct tcm_device *d=cookie;
    if(!value || !marble_tcm_read_allowed(offset,4))return -1;
    *value=power_read((void *)d->gmu,offset);return 0;
}
static int tcm_write(void *cookie,unsigned offset,uint32_t value)
{
    struct tcm_device *d=cookie;
    if(!marble_tcm_write_allowed(offset,4,probe_stage,probe_stage==17?boot_tcm_lease:1))return -1;
    /* Do not let the compiler emit STR WZR: EFI's abort decoder needs w9. */
    __asm__ volatile("mov w9, %w0\n\tstr w9, [%1]"::"r"(value),"r"(d->gmu+offset):"x9","memory");
    return 0;
}
static int tcm_barrier(void *cookie)
{(void)cookie;__asm__ volatile("dsb sy":::"memory");return 0;}
static int tcm_guard(void *cookie)
{
    struct tcm_device *d=cookie;
    uint32_t reset=power_read((void *)d->gmu,0x16000),fw=power_read((void *)d->gmu,0x16070);
    uint32_t cx=power_read((void *)d->cc,MGP_CX_STATUS),gx=power_read((void *)d->cc,MGP_GX);
    return reset!=1 || fw!=0 || !(cx&0x80000000u) || !(gx&1) || (gx&0x80000000u);
}
static int clock_read(void *cookie,unsigned offset,uint32_t *value)
{
    struct tcm_device *d=cookie;
    if(!value || !marble_gmu_clock_read_allowed(offset,4))return -1;
    *value=power_read((void *)d->cc,offset);return 0;
}
static void vote_payload_log(const char *name,unsigned id,const uint32_t *data,size_t words)
{
    SHA256_CTX ctx;uint8_t digest[32];char hex[65];
    SHA256_Init(&ctx);SHA256_Update(&ctx,data,words*4);SHA256_Final(digest,&ctx);
    for(unsigned i=0;i<32;i++){hex[2*i]="0123456789abcdef"[digest[i]>>4];hex[2*i+1]="0123456789abcdef"[digest[i]&15];}hex[64]=0;
    marble_gpu_log("marble-gmu-votes: payload name=%s id=%u words=%lu sha256=%s\n",name,id,words,hex);
    for(size_t offset=0;offset<words;offset+=8) {
        char chunk[65];size_t count=words-offset;if(count>8)count=8;
        for(size_t j=0;j<count;j++)for(unsigned k=0;k<8;k++)
            chunk[j*8+k]="0123456789abcdef"[(data[offset+j]>>(28-4*k))&15];
        chunk[count*8]=0;
        marble_gpu_log("marble-gmu-votes: words name=%s offset=%lu data=%s\n",name,offset,chunk);
    }
}
static int gmu_votes(struct tcm_device *device)
{
    if(tcm_guard(device) || !marble_gmu_vote_plan(cmd_db_snapshot,sizeof(cmd_db_snapshot),&vote_plan))return -1;
    marble_gpu_log("marble-gmu-votes: catalog gpu=8 gmu=3 bandwidth=12 enabled_OPPs=0; DT candidates only\n");
    vote_payload_log("performance",vote_plan.performance_id,vote_plan.performance,vote_plan.performance_words);
    vote_payload_log("bandwidth",vote_plan.bandwidth_id,vote_plan.bandwidth,vote_plan.bandwidth_words);
    int stopped=!tcm_guard(device) && !hfi_notify_attempts && !hfi_transport.pending_header &&
        !hfi_transport.sequence && !hfi_transport.poisoned;
    marble_gpu_log("marble-gmu-votes: " MARBLE_GMU_VOTES_ABI " PASS=%d cpu_only=1 queue_writes=0 hardware_votes=0 notifications=%u reset_released=0\n",stopped,hfi_notify_attempts);
    return stopped?0:-1;
}
struct clock_device {struct tcm_device *device;vm_offset_t gcc;};
static int board_clock_read(void *cookie,unsigned block,unsigned offset,uint32_t *value)
{
    struct clock_device *d=cookie;if(!value || !marble_clock_access(block,offset,4,0))return -1;
    *value=power_read((void *)(block?d->device->cc:d->gcc),offset);return 0;
}
static int board_clock_write(void *cookie,unsigned block,unsigned offset,uint32_t value)
{
    struct clock_device *d=cookie;if(!marble_clock_access(block,offset,4,1))return -1;
    vm_offset_t address=(block?d->device->cc:d->gcc)+offset;
    __asm__ volatile("mov w9, %w0\n\tstr w9, [%1]\n\tdsb sy"::"r"(value),"r"(address):"x9","memory");return 0;
}
static int board_clock_guard(void *cookie)
{return tcm_guard(((struct clock_device *)cookie)->device);}
static int gmu_clocks(struct tcm_device *device)
{
    struct clock_device d={device,ml_io_map(0x00100000,0x80000)};if(!d.gcc)return -1;
    MarbleClockIo io={&d,board_clock_read,board_clock_write,power_delay,board_clock_guard};MarbleClockResult r;
    int passed=marble_clock_transaction(&io,&r);
    for(unsigned i=0;i<13;i++)marble_gpu_log("marble-gmu-clocks: register block=%u offset=%#x before=%#x after=%#x\n",
        marble_clock_block[i],marble_clock_reg[i],r.before[i],r.after[i]);
    for(unsigned i=0;i<2;i++)marble_gpu_log("marble-gmu-clocks: measurement index=%u short=%u full=%u hz=%u\n",i,r.short_count[i],r.full_count[i],r.hz[i]);
    marble_gpu_log("marble-gmu-clocks: " MARBLE_GMU_CLOCKS_ABI " PASS=%d configured=%u measured=%u restored=%u retain=%u refused=%u phase=%u reset_released=0\n",
        passed,r.configured,r.measured,r.restored,r.retain,r.refused,r.phase);
    if(r.retain)panic("marble GMU clock cleanup unverified; cold reset required");
    return passed?0:-1;
}
struct rpmh_device {struct tcm_device *device;vm_offset_t base;};
static int board_rpmh_read(void *cookie,unsigned offset,uint32_t *value)
{
    struct rpmh_device *d=cookie;
    if(!value || !marble_rpmh_access(offset,4,0))return -1;
    *value=power_read((void *)d->base,offset);return 0;
}
static int board_rpmh_write(void *cookie,unsigned offset,uint32_t value)
{
    struct rpmh_device *d=cookie;
    if(!marble_rpmh_access(offset,4,1))return -1;
    vm_offset_t address=d->base+offset;
    __asm__ volatile("mov w9, %w0\n\tstr w9, [%1]\n\tdsb sy"::"r"(value),"r"(address):"x9","memory");return 0;
}
static int board_rpmh_guard(void *cookie)
{return tcm_guard(((struct rpmh_device *)cookie)->device);}
static int gmu_rpmh(struct tcm_device *device)
{
    uint32_t addresses[4];static const char *const names[]={"cx.lvl","mx.lvl","gfx.lvl","mxc.lvl"};
    for(unsigned i=0;i<4;i++) {
        MarbleCmdDbEntry e;
        if(!marble_cmd_db_find(cmd_db_snapshot,sizeof(cmd_db_snapshot),names[i],&e) || e.slave!=3)return -1;
        addresses[i]=e.address;
    }
    struct rpmh_device d={device,ml_io_map(MARBLE_RPMH_PHYS,0x100)};if(!d.base)return -1;
    MarbleRpmhIo io={&d,board_rpmh_read,board_rpmh_write,power_delay,board_rpmh_guard};MarbleRpmhResult r;
    int passed=marble_rpmh_snapshot(&io,addresses,&r);
    for(unsigned i=0;i<7;i++)marble_gpu_log("marble-gmu-rpmh: register offset=%#x before=%#x after=%#x\n",marble_rpmh_saved[i],r.before[i],r.after[i]);
    for(unsigned i=0;i<8;i++)marble_gpu_log("marble-gmu-rpmh: response index=%u name=%s address=%#x data=%#x status=%#x\n",i,names[i/2],addresses[i/2],r.response[i],r.cmd_status[i]);
    marble_gpu_log("marble-gmu-rpmh: " MARBLE_GMU_RPMH_ABI " PASS=%d reads=%u completed=%u changed=%u restored=%u retain=%u refused=%u phase=%u rail_writes=0 reset_released=0\n",passed,r.reads,r.completed,r.changed,r.restored,r.retain,r.refused,r.phase);
    if(r.retain)panic("marble RPMh command completion or cleanup unverified; cold reset required");
    if(passed)rpmh_arc_snapshot=r;
    return passed?0:-1;
}
static int gmu_dependencies(struct tcm_device *device)
{
    static const char *const names[]={"CN0","SH0","MC0","ACV"};uint32_t addresses[4],levels[2];
    for(unsigned i=0;i<4;i++) {
        MarbleCmdDbEntry e;
        if(!marble_cmd_db_find(cmd_db_snapshot,sizeof(cmd_db_snapshot),names[i],&e) || e.slave!=5)return -1;
        addresses[i]=e.address;
    }
    for(unsigned i=0;i<2;i++) {
        MarbleCmdDbEntry e;
        if(!marble_cmd_db_find(cmd_db_snapshot,sizeof(cmd_db_snapshot),i?"mx.lvl":"cx.lvl",&e) ||
            !marble_arc_level(&e,rpmh_arc_snapshot.response[i*2],levels+i))return -1;
    }
    struct rpmh_device d={device,ml_io_map(MARBLE_RPMH_PHYS,0x100)};if(!d.base)return -1;
    MarbleRpmhIo io={&d,board_rpmh_read,board_rpmh_write,power_delay,board_rpmh_guard};MarbleRpmhResult r;
    int passed=marble_rpmh_snapshot_class(&io,addresses,5,&r);
    for(unsigned i=0;i<7;i++)marble_gpu_log("marble-gmu-dependencies: register offset=%#x before=%#x after=%#x\n",marble_rpmh_saved[i],r.before[i],r.after[i]);
    for(unsigned i=0;i<8;i++)marble_gpu_log("marble-gmu-dependencies: response index=%u name=%s address=%#x data=%#x status=%#x\n",i,names[i/2],addresses[i/2],r.response[i],r.cmd_status[i]);
    unsigned adequate=passed && levels[0]>=64 && levels[1]>=64;
    for(unsigned i=0;i<4;i++) {
        uint32_t minimum=i?vote_plan.config.ddr_data[1][i-1]:vote_plan.config.cnoc_data[1][0];
        unsigned covers=i==3?((r.response[i*2]&0x20000000u) && (r.response[i*2]&minimum&0x3fffu)==(minimum&0x3fffu)):
            marble_bcm_covers(r.response[i*2],minimum);
        marble_gpu_log("marble-gmu-dependencies: minimum name=%s data=%#x covers=%u\n",names[i],minimum,covers);
        adequate&=covers;
    }
    marble_gpu_log("marble-gmu-dependencies: " MARBLE_GMU_DEPENDENCIES_ABI " PASS=%d cx_level=%u mx_level=%u adequate=%u reads=%u completed=%u restored=%u retain=%u phase=%u hardware_votes=0 reset_released=0\n",passed,levels[0],levels[1],adequate,r.reads,r.completed,r.restored,r.retain,r.phase);
    if(r.retain)panic("marble BCM read completion or cleanup unverified; cold reset required");
    return adequate?0:-1;
}
struct boot_device {struct tcm_device tcm;vm_offset_t gpu,misc,rsc,smmu;};
static int board_boot_read(void *cookie,unsigned block,unsigned offset,uint32_t *value)
{
    struct boot_device *d=cookie;
    if(!value || !marble_boot_access(block,offset,4,0))return -1;
    vm_offset_t base=block==0?d->tcm.gmu:block==1?d->gpu:block==2?d->misc:d->rsc;
    *value=power_read((void *)base,offset);return 0;
}
static int board_boot_write(void *cookie,unsigned block,unsigned offset,uint32_t value)
{
    struct boot_device *d=cookie;
    if(!marble_boot_access(block,offset,4,1))return -1;
    vm_offset_t base=block==0?d->tcm.gmu:block==1?d->gpu:block==2?d->misc:d->rsc;
    marble_gpu_log("marble-gmu-step: before-write block=%u offset=%#x value=%#x\n",block,offset,value);
    delay(offset==0x16000?500000:20000); /* preserve boundary before reset release */
    __asm__ volatile("mov w9, %w0\n\tstr w9, [%1]\n\tdsb sy"::"r"(value),"r"(base+offset):"x9","memory");
    marble_gpu_log("marble-gmu-step: after-write block=%u offset=%#x\n",block,offset);return 0;
}
static int board_boot_health(void *cookie)
{
    struct boot_device *d=cookie;
    uint32_t cx=power_read((void *)d->tcm.cc,MGP_CX_STATUS),gx=power_read((void *)d->tcm.cc,MGP_GX);
    int bad=marble_gpu_smmu_health() || !(cx&0x80000000u) || (!boot_gx_lease && (!(gx&1) || (gx&0x80000000u))) ||
        (power_read((void *)d->tcm.cc,MGP_GMU)&0x80000000u) ||
        (power_read((void *)d->tcm.cc,MGP_HUB)&0x80000000u) ||
        (power_read((void *)d->tcm.cc,0x931c)&0x10371fu)!=0x100602 ||
        (power_read((void *)d->tcm.cc,0x93f0)&0x10371fu)!=0x100603 ||
        !(power_read((void *)d->smmu,MARBLE_SMMU_CB0)&1) ||
        power_read((void *)d->smmu,0x800)!=0x80000005u || power_read((void *)d->smmu,0xc00)!=0 ||
        power_read((void *)d->smmu,0x804)!=0x80000405u || power_read((void *)d->smmu,0xc04)!=0 ||
        (power_read((void *)d->smmu,MARBLE_SMMU_CB0+0x58)&0xc00001feu) || power_read((void *)d->smmu,0x48);
    static unsigned reported;
    if(bad && !reported++) {
        marble_gpu_log("marble-gmu-health: FAIL cx=%#x gx=%#x lease=%u gmu_clk=%#x hub_clk=%#x gmu_cfg=%#x hub_cfg=%#x SCTLR=%#x FSR=%#x GFSR=%#x\n",cx,gx,boot_gx_lease,power_read((void *)d->tcm.cc,MGP_GMU),power_read((void *)d->tcm.cc,MGP_HUB),power_read((void *)d->tcm.cc,0x931c),power_read((void *)d->tcm.cc,0x93f0),power_read((void *)d->smmu,MARBLE_SMMU_CB0),power_read((void *)d->smmu,MARBLE_SMMU_CB0+0x58),power_read((void *)d->smmu,0x48));
    }
    return bad;
}
static int board_hfi_read(void *cookie,unsigned reg,uint32_t *v)
{
    struct boot_device *d=cookie;
    if(!v || !marble_hfi_access(reg,0))return -1;
    *v=power_read((void *)d->tcm.gmu,reg);return 0;
}
static int board_hfi_write(void *cookie,unsigned reg,uint32_t v)
{
    struct boot_device *d=cookie;
    if(!marble_hfi_access(reg,1))return -1;
    __asm__ volatile("mov w9, %w0\n\tstr w9, [%1]\n\tdsb sy"::"r"(v),"r"(d->tcm.gmu+reg):"x9","memory");
    return 0;
}
static int board_hfi_notify(void *cookie)
{hfi_notify_attempts++;return board_hfi_write(cookie,0x16650,1);}
static int board_gx_read(void *cookie,unsigned block,unsigned reg,uint32_t *v)
{
    struct boot_device *d=cookie;if(!v || !marble_gx_access(block,reg,0))return -1;
    vm_offset_t base=block==0?d->tcm.gmu:block==1?d->gpu:d->tcm.cc;
    *v=power_read((void *)base,reg);return 0;
}
static int board_gx_write(void *cookie,unsigned block,unsigned reg,uint32_t v)
{
    struct boot_device *d=cookie;if(!marble_gx_access(block,reg,1))return -1;
    vm_offset_t base=block==0?d->tcm.gmu:block==1?d->gpu:d->tcm.cc;
    __asm__ volatile("mov w9, %w0\n\tstr w9, [%1]\n\tdsb sy"::"r"(v),"r"(base+reg):"x9","memory");
    return 0;
}
static MarbleCpResult cp_result;
static MarbleZapResult zap_result;
static int board_zap_stop(void *cookie);
static int board_cp_bus_restore(void *cookie);
static int board_gx_stop(void *cookie,unsigned phase)
{
    MarbleGxIo io={cookie,board_gx_read,board_gx_write,power_delay,board_boot_health};
    int rc=marble_gx_stop(&io,&gx_result,phase);
    if(phase==1) {
        for(unsigned i=0;i<5;i++)marble_gpu_log("marble-gmu-gx: register offset=%#x before=%#x after=%#x\n",marble_gx_saved[i],gx_result.before[i],gx_result.after[i]);
        marble_gpu_log("marble-gmu-gx: stopped rc=%d halted=%u halt_ack=%#x collapsed=%u restored=%u phase=%u\n",rc,gx_result.halted,gx_result.halt_ack,gx_result.collapsed,gx_result.restored,gx_result.phase);
        if(!rc && !board_zap_stop(cookie))rc=-1;
        if(!rc && !board_cp_bus_restore(cookie))rc=-1;
        if(!rc)boot_gx_lease=0;
    }
    return rc;
}
static int board_cp_read(void *cookie,unsigned reg,uint32_t *v)
{
    struct boot_device *d=cookie;if(!v || !marble_cp_access(reg,0))return -1;
    *v=power_read((void *)d->gpu,reg*4);return 0;
}
static int board_cp_write(void *cookie,unsigned reg,uint32_t v)
{
    struct boot_device *d=cookie;if(!marble_cp_access(reg,1))return -1;
    if(!runtime_active) {
        marble_gpu_log("marble-gpu-cp: before-write reg=%#x value=%#x\n",reg,v);
        delay(100000); /* Diagnostic capture boundary, never per runtime kick. */
    }
    __asm__ volatile("mov w9, %w0\n\tstr w9, [%1]\n\tdsb sy"::"r"(v),"r"(d->gpu+reg*4):"x9","memory");
    if(!runtime_active)marble_gpu_log("marble-gpu-cp: after-write reg=%#x\n",reg);
    return 0;
}
static void board_cp_barrier(void *cookie)
{(void)cookie;__asm__ volatile("dsb sy":::"memory");}
static int board_runtime_idle_at(void *cookie,uint32_t position,uint32_t fence)
{
    struct boot_device *d=cookie;
    if(!runtime_gpu_started) {
        /* Before firmware release no master may fetch any GPU mapping. */
        return power_read((void *)d->tcm.gmu,0x16000)==1 &&
            (power_read((void *)d->tcm.cc,MGP_GX)&0x80000001u)==1;
    }
    uint32_t rptr=0,wptr=0,status=0,status3=0,fault=0,irq=0;
    volatile uint32_t *mem=(volatile uint32_t *)(gpu_uncached+0x24000);
    board_cp_barrier(cookie);
    if(board_boot_health(cookie) || board_cp_read(cookie,0x806,&rptr) ||
        board_cp_read(cookie,0x807,&wptr) || board_cp_read(cookie,0x210,&status) ||
        board_cp_read(cookie,0x213,&status3) || board_cp_read(cookie,0x821,&fault) ||
        board_cp_read(cookie,0x201,&irq))return 0;
    return rptr==position && wptr==position && mem[0]==position &&
        mem[1]==fence && !(status&~1u) && !status3 && !fault && !(irq&0x138003c2u);
}
static int board_runtime_quiesce(void *cookie)
{
    /* Live SMMU publication/removal still requires a genuinely idle master. */
    if(service_pending.active)service_complete(1);
    return board_runtime_idle_at(cookie,runtime_position,runtime_fence);
}
static int runtime_tables_prepare(void)
{
    uint64_t pa=kvtophys((vm_offset_t)runtime_tables);
    if(!pa || (pa&0x3fff) || pa+sizeof(runtime_tables)>(UINT64_C(1)<<36))return 0;
    for(size_t i=0;i<sizeof(runtime_tables);i+=4096)
        if(kvtophys((vm_offset_t)runtime_tables+i)!=pa+i)return 0;
    runtime_arena=(ArmSmmu4KLiveArena){.config={32,36,pa,0x20000000/4096},
        .iova=0,.bytes=0x20000000,.mapping=runtime_tables,.capacity_pages=260,
        .alias_scratch=runtime_alias_scratch,.alias_scratch_entries=MARBLE_GPU_LIVE_RANGES};
    int ok=!ArmSmmu4KLivePrepare(&runtime_arena,&runtime_table_state);
    marble_gpu_log("marble-gpu-live-map: MGPU-MARBLE-LIVE-MAP-v1 prepared=%u table_PA=%#llx pages=%lu aperture=%#llx leaves=%lu\n",ok,pa,(unsigned long)runtime_table_state.tables.table_pages,runtime_arena.bytes,(unsigned long)runtime_table_state.tables.leaf_pages);
    return ok;
}
static uint8_t zap_metadata[0x4000] __attribute__((aligned(0x4000)));
static uint64_t zap_metadata_pa;
static int board_zap_scm(void *cookie,uint32_t command,uint32_t args,
    uint64_t first,uint64_t second,uint64_t third,uint64_t *answer)
{
    (void)cookie;
    if(!answer || (command!=0x601 && command!=0x201 && command!=0x202 && command!=0x205 && command!=0x206))return -1;
    if(command==0x601) {
        if(args!=1 || (first!=0x02000201 && first!=0x02000202 && first!=0x02000205 && first!=0x02000206) || second || third)return -1;
    } else if(first!=13 || (command==0x201?(args!=0x82 || second!=zap_metadata_pa || third):
        command==0x202?(args!=3 || second!=MARBLE_ZAP_PHYS || third!=MARBLE_ZAP_BYTES):
        (args!=1 || second || third)))return -1;
    uint64_t function=(command==0x601?UINT64_C(0xc2000000):UINT64_C(0x42000000))|command;
    uint64_t session=0;
    marble_gpu_log("marble-gpu-zap: before-scm command=%#x args=%#x first=%#llx second=%#llx third=%#llx\n",command,args,first,second,third);
    delay(100000);
    for(unsigned i=0;i<100;i++) {
        register uint64_t x0 __asm__("x0")=i?1:function;
        register uint64_t x1 __asm__("x1")=args;
        register uint64_t x2 __asm__("x2")=first;
        register uint64_t x3 __asm__("x3")=second;
        register uint64_t x4 __asm__("x4")=third;
        register uint64_t x5 __asm__("x5")=0;
        register uint64_t x6 __asm__("x6")=session;
        register uint64_t x7 __asm__("x7")=0;
        __asm__ volatile("smc #0":"+r"(x0),"+r"(x1),"+r"(x2),"+r"(x3),"+r"(x4),"+r"(x5),"+r"(x6),"+r"(x7)::
            "x8","x9","x10","x11","x12","x13","x14","x15","x16","x17","memory");
        session=x6;
        if(x0!=1) {
            *answer=x1;
            marble_gpu_log("marble-gpu-zap: after-scm command=%#x status=%d answer=%#llx retries=%u\n",command,(int32_t)x0,x1,i);
            return (int32_t)x0;
        }
    }
    marble_gpu_log("marble-gpu-zap: interrupted-limit command=%#x\n",command);return -1;
}
static int board_zap_copy(void *cookie,uint64_t address,unsigned bytes)
{
    (void)cookie;
    if(address!=MARBLE_ZAP_PHYS || bytes!=MARBLE_ZAP_BYTES || files[4].bytes!=2236)return -1;
    vm_offset_t va=ml_io_map_wcomb(address,bytes);if(!va)return -1;
    volatile uint8_t *p=(volatile uint8_t *)va;
    for(unsigned i=0;i<bytes;i++)p[i]=i<files[4].bytes?files[4].data[i]:0;
    __asm__ volatile("dsb sy":::"memory");
    for(unsigned i=0;i<bytes;i++)if(p[i]!=(i<files[4].bytes?files[4].data[i]:0))return -1;
    marble_gpu_log("marble-gpu-zap: payload PASS=1 address=%#llx bytes=%u file_bytes=2236 segment=1 readback=1\n",address,bytes);
    return 0;
}
static int board_zap_test(void *cookie)
{
    /* Exact locked a730 ELF facts checked before any PAS call or carveout map. */
    const uint8_t *ph=files[2].data+84;
    if(files[2].bytes!=4044 || marble_gpu_le32(ph)!=1 || marble_gpu_le32(ph+4)!=0x1000 ||
        marble_gpu_le32(ph+12)!=0x1000 || marble_gpu_le32(ph+16)!=2236 ||
        marble_gpu_le32(ph+20)!=2236 || marble_gpu_le32(ph+24)!=0x08000007)return 0;
    zap_metadata_pa=kvtophys((vm_offset_t)zap_metadata);
    if(!zap_metadata_pa || (zap_metadata_pa&0x3fff))return 0;
    memset(zap_metadata,0,sizeof(zap_metadata));memcpy(zap_metadata,files[2].data,files[2].bytes);
    uint64_t ctr;__asm__ volatile("mrs %0, ctr_el0":"=r"(ctr));size_t line=(size_t)4<<((ctr>>16)&15);
    for(vm_offset_t va=(vm_offset_t)zap_metadata;va<(vm_offset_t)zap_metadata+sizeof(zap_metadata);va+=line)
        __asm__ volatile("dc civac, %0"::"r"(va):"memory");
    __asm__ volatile("dsb sy":::"memory");
    MarbleZapIo io={cookie,board_zap_scm,board_zap_copy,board_boot_health};
    int ok=marble_zap_transaction(&io,zap_metadata_pa,&zap_result);
    marble_gpu_log("marble-gpu-zap: " MARBLE_ZAP_ABI " PASS=%u available=%u old_unloaded=%u attempted=%u initialized=%u memory_ready=%u copied=%u authenticated=%u unloaded=%u retain=%u phase=%u status=%d answer=%#llx\n",ok,zap_result.available,zap_result.old_unloaded,zap_result.attempted,zap_result.initialized,zap_result.memory_ready,zap_result.copied,zap_result.authenticated,zap_result.unloaded,zap_result.retain,zap_result.phase,zap_result.status,zap_result.answer);
    return ok;
}
static int board_zap_stop(void *cookie)
{
    MarbleZapIo io={cookie,board_zap_scm,board_zap_copy,board_boot_health};
    int ok=marble_zap_shutdown(&io,&zap_result,gx_result.collapsed && gx_result.halted);
    marble_gpu_log("marble-gpu-zap: shutdown PASS=%u attempted=%u unloaded=%u retain=%u phase=%u\n",ok,zap_result.attempted,zap_result.unloaded,zap_result.retain,zap_result.phase);
    return ok;
}
static int board_cp_bus_restore(void *cookie)
{
    MarbleCpIo io={cookie,board_cp_read,board_cp_write,power_delay,board_boot_health,board_cp_barrier};
    int ok=marble_cp_restore_bus(&io,&cp_result,gx_result.collapsed && gx_result.halted);
    marble_gpu_log("marble-gpu-cp: CX-bus-restore PASS=%u changed=%u restored=%u\n",ok,cp_result.bus_changed,cp_result.bus_restored);
    return ok;
}
static int board_runtime_dynamic(const MarbleCpIo *io,MarbleBlitResult *b)
{
    /* Hardware qualification of LiveUpdateContextBatch while every master is
     * demonstrably idle. Only these owned 16KB are added, drawn and removed. */
    uint64_t pa=kvtophys((vm_offset_t)runtime_target);
    if(!pa || (pa&0x3fff) || pa+sizeof(runtime_target)>(UINT64_C(1)<<36))return 0;
    for(size_t i=0;i<sizeof(runtime_target);i+=4096)
        if(kvtophys((vm_offset_t)runtime_target+i)!=pa+i)return 0;
    memset(runtime_target,0,sizeof(runtime_target));
    uint64_t ctr;__asm__ volatile("mrs %0, ctr_el0":"=r"(ctr));size_t line=(size_t)4<<((ctr>>16)&15);
    for(vm_offset_t va=(vm_offset_t)runtime_target;va<(vm_offset_t)runtime_target+sizeof(runtime_target);va+=line)
        __asm__ volatile("dc civac, %0"::"r"(va):"memory");
    __asm__ volatile("dsb sy":::"memory");
    vm_offset_t target=ml_io_map_wcomb(pa,sizeof(runtime_target));if(!target)return 0;
    ArmSmmu4KRange range={0x2000000,pa,sizeof(runtime_target),ArmSmmu4KRead|ArmSmmu4KWrite,0,2};
    unsigned cycles=0;
    for(;cycles<64;cycles++) {
        if(!marble_gpu_smmu_live_update(&range,1,1) ||
            !marble_gpu_smmu_live_ecats(range.iova,pa) ||
            !marble_gpu_smmu_live_ecats(range.iova+0x3000,pa+0x3000))return 0;
        if(!marble_blit_submit_target(io,(volatile uint32_t *)(gpu_uncached+0x20000),
            (volatile uint32_t *)(gpu_uncached+0x24000),(volatile uint32_t *)target,
            runtime_position,runtime_fence+1,0x2001000,b))return 0;
        runtime_position=b->wptr;runtime_fence=b->fence;
        if(!marble_gpu_smmu_live_update(&range,1,0))return 0;
    }
    marble_gpu_log("marble-gpu-live-map: cycles PASS=1 cycles=%u map=64 unmap=64 ecats=128 draws=64 pixels=%u guards=%u pos=%u fence=%u generation=%llu leaves=%lu\n",cycles,b->pixels,b->guards,b->wptr,b->fence,runtime_table_state.generation,(unsigned long)runtime_table_state.tables.leaf_pages);
    return 1;
}
static int service_barrier(void *cookie){board_cp_barrier(cookie);return 0;}
void marble_gpu_report(const char *why)
{
    uint32_t rp=0,wp=0,status=0,status3=0,fault=0,irq=0,ib=0,remaining=0;
    if(service_cookie) {
        board_cp_read(service_cookie,0x806,&rp);board_cp_read(service_cookie,0x807,&wp);
        board_cp_read(service_cookie,0x210,&status);board_cp_read(service_cookie,0x213,&status3);
        board_cp_read(service_cookie,0x821,&fault);board_cp_read(service_cookie,0x201,&irq);
        board_cp_read(service_cookie,0x809,&ib);board_cp_read(service_cookie,0x80b,&remaining);
    }
    marble_gpu_log("marble-gpu-service: report why=%s submissions=%u pos=%u fence=%u rptr=%u wptr=%u status=%#x status3=%#x fault=%#x irq=%#x IB1=%#x remaining=%u\n",
        why,service_submissions,runtime_position,runtime_fence,rp,wp,status,status3,fault,irq,ib,remaining);
    if(service_pending.active)marble_gpu_log("marble-gpu-service: pending pos=%u fence=%u failed=%u\n",
        service_pending.position,service_pending.fence,service_pending.failed);
}
static uint64_t service_now_us(void *cookie)
{(void)cookie;uint64_t ns;absolutetime_to_nanoseconds(mach_absolute_time(),&ns);return ns/1000;}
static int service_sample(void *cookie,uint32_t position,uint32_t fence)
{
    board_cp_barrier(cookie);
    if(board_boot_health(cookie))return -1;
    volatile uint32_t *mem=(volatile uint32_t *)(gpu_uncached+0x24000);
    if(mem[1]!=fence || mem[0]!=position)return 0;
    return board_runtime_idle_at(cookie,position,fence);
}
static int service_retire(void *cookie,uint32_t fence)
{(void)cookie;return marble_ring_retire(fence);}
static int service_complete(unsigned wait)
{
    if(!service_pending.active)return 1;
    MarbleGpuPendingIo io={service_cookie,service_now_us,service_sample,service_retire};
    for(;;) {
        int state=marble_pending_observe(&service_pending,&io);
        if(state<0) {
            marble_gpu_report("completion timeout or fault");
            panic("marble GPU pending batch uncertain; retain all BOs until reset");
        }
        if(state>0) {
            runtime_position=service_pending.position;runtime_fence=service_pending.fence;
            return 1;
        }
        if(!wait)return 0;
        delay(10);
    }
}
uint32_t marble_gpu_submit(const uint64_t *addresses,const uint32_t *sizes,uint32_t count)
{
    if(!service_accepting || !addresses || !sizes || !count || count>128)return 0;
    for(unsigned i=0;i<count;i++)if(addresses[i]<0x4000000 || addresses[i]>=0x20000000 ||
        (addresses[i]&3) || !sizes[i] || sizes[i]>(0x20000000-addresses[i])/4)return 0;
    /* At most four ordered batches; even four maximum-size publications
     * (4*128 IB words + 68 framing words each) fit the 4096-word ring.
     * Cache maintenance and SMMU changes still drain the entire group. */
    service_complete(0);
    if(service_pending.active && service_pending.depth>=MARBLE_GPU_PENDING_MAX)service_complete(1);
    if(!service_pending.active && !board_runtime_idle_at(service_cookie,runtime_position,runtime_fence))
        panic("marble GPU before first queued submit not quiescent");
    uint32_t position=0,fence=0;
    if(!marble_ring_submit(addresses,sizes,count,&position,&fence))
        panic("marble GPU uncertain ring publication; retain all BOs until reset");
    if(!marble_pending_append(&service_pending,position,fence,service_now_us(NULL)))
        panic("marble GPU bounded queue ownership failure; retain all BOs until reset");
    service_submissions++;
    if(service_submissions<=8 || !(service_submissions%128))
        marble_gpu_log("marble-gpu-service: queued=1 count=%u submissions=%u pos=%u fence=%u depth=%u\n",count,service_submissions,position,fence,service_pending.depth);
    return fence;
}
static int board_runtime_work(const MarbleCpIo *io,MarbleBlitResult *b)
{
    runtime_active=1;
    runtime_gpu_started=1;runtime_position=b->wptr;runtime_fence=b->fence;
    if(!board_runtime_dynamic(io,b)) {
        runtime_active=0;marble_gpu_log("marble-gpu-live-map: cycles PASS=0 pos=%u fence=%u\n",runtime_position,runtime_fence);
        return 0;
    }
    service_cookie=io->cookie;
    marble_gpu_log("marble-gpu-service: adopting completed ring pos=%u fence=%u\n",runtime_position,runtime_fence);
    if(!board_runtime_quiesce(service_cookie) || !marble_ring_adopt(service_cookie,
        (void *)(gpu_uncached+0x20000),(void *)(gpu_uncached+0x24000),runtime_position,runtime_fence,
        board_cp_write,board_cp_read,service_barrier)) {
        runtime_active=0;marble_gpu_log("marble-gpu-service: adoption FAIL; no ring writes; unwind leases\n");return 0;
    }
    marble_gpu_log("marble-gpu-service: idle ring adopted; starting IOKit service\n");delay(100000);
    redwood_gpu_marble_allocator();service_accepting=1;
    __atomic_store_n(&service_started,1,__ATOMIC_RELEASE);
    RedwoodGPUStart();
    marble_gpu_log("marble-gpu-runtime: MGPU-MARBLE-RUNTIME-v1 ready=1 service=1 pos=%u fence=%u\n",runtime_position,runtime_fence);
    marble_gpu_log("marble-gpu-service: MGPU-MARBLE-SERVICE-v1 ready=1 chip=0x07030002 gmem=2097152 VA=0x4000000 size=0x1c000000 pos=%u fence=%u\n",runtime_position,runtime_fence);
    while(!__atomic_load_n(&runtime_stop_requested,__ATOMIC_ACQUIRE))delay_for_interval(10,NSEC_PER_MSEC);
    runtime_active=0;
    marble_gpu_log("marble-gpu-service: work PASS=1 stop_requested=1 submissions=%u pos=%u fence=%u\n",service_submissions,runtime_position,runtime_fence);
    return board_runtime_quiesce(service_cookie);
#if 0 /* stable persistent-worker diagnostic remains in its saved baseline */
    marble_gpu_log("marble-gpu-runtime: MGPU-MARBLE-RUNTIME-v1 ready=1 service=0 pos=%u fence=%u\n",b->wptr,b->fence);
    int ok=1;
    while(!__atomic_load_n(&runtime_stop_requested,__ATOMIC_ACQUIRE)) {
        delay_for_interval(100,NSEC_PER_MSEC);
        if(__atomic_load_n(&runtime_stop_requested,__ATOMIC_ACQUIRE))break;
        if(io->health(io->cookie)){ok=0;break;}
        unsigned previous=b->wptr;uint32_t fence=b->fence+1;
        if(!marble_blit_submit(io,(volatile uint32_t *)(gpu_uncached+0x20000),
            (volatile uint32_t *)(gpu_uncached+0x24000),(volatile uint32_t *)(gpu_uncached+0x2c000),previous,fence,b)) {
            marble_gpu_log("marble-gpu-runtime: draw FAIL pos=%u fence=%u rptr=%u wptr=%u shadow=%u observed_fence=%u fault=%#x irq=%#x bad_index=%u bad_value=%#x\n",previous,fence,b->rptr,b->wptr,b->shadow,b->fence,b->fault,b->irq,b->bad_index,b->bad_value);
            ok=0;break;
        }
        runtime_draws++;runtime_wraps+=b->wptr<previous;
        runtime_position=b->wptr;runtime_fence=b->fence;
        if(!(runtime_draws%32))marble_gpu_log("marble-gpu-runtime: progress draws=%u wraps=%u pos=%u fence=%u pixels=%u guards=%u fault=%#x irq=%#x\n",runtime_draws,runtime_wraps,b->wptr,b->fence,b->pixels,b->guards,b->fault,b->irq);
    }
    runtime_active=0;
    marble_gpu_log("marble-gpu-runtime: work PASS=%u stop_requested=%u draws=%u wraps=%u pos=%u fence=%u pixels=%u guards=%u\n",ok,__atomic_load_n(&runtime_stop_requested,__ATOMIC_ACQUIRE),runtime_draws,runtime_wraps,b->wptr,b->fence,b->pixels,b->guards);
    return ok;
#endif
}
static int board_cp_test(void *cookie)
{
    MarbleCpIo io={cookie,board_cp_read,board_cp_write,power_delay,board_boot_health,board_cp_barrier};MarbleCpResult r;
    int passed=marble_cp_run(&io,(volatile uint32_t *)(gpu_uncached+0x20000),
        (volatile uint32_t *)(gpu_uncached+0x24000),(volatile uint32_t *)(gpu_uncached+0x2c000),0x4d475031,gx_result.held,&r);
    if(passed && !board_zap_test(cookie))passed=0;
    if(passed) {
        MarbleSecureResult s;
        int secure=marble_secure_run(&io,(volatile uint32_t *)(gpu_uncached+0x20000),
            (volatile uint32_t *)(gpu_uncached+0x24000),&r,zap_result.authenticated,&s);
        marble_gpu_log("marble-gpu-secure: MGPU-MARBLE-SECURE-v1 PASS=%u before=%#x after=%#x submitted=%u completed=%u polls=%u rptr=%u wptr=%u shadow=%u fence=%u status=%#x status3=%#x irq=%#x fault=%#x phase=%u\n",secure,s.before,s.after,s.submitted,s.completed,s.polls,s.rptr,s.wptr,s.shadow,s.fence,s.status,s.status3,s.irq,s.fault,s.phase);
        if(!secure)passed=0;
        if(secure) {
            MarbleBlitResult b;
            int blit=marble_blit_run(&io,(volatile uint32_t *)(gpu_uncached+0x20000),
                (volatile uint32_t *)(gpu_uncached+0x24000),(volatile uint32_t *)(gpu_uncached+0x2c000),&s,&b);
            marble_gpu_log("marble-gpu-blit: MGPU-MARBLE-BLIT-v1 PASS=%u submitted=%u completed=%u words=%u polls=%u rptr=%u wptr=%u shadow=%u fence=%u status=%#x status3=%#x irq=%#x fault=%#x pixels=%u guards=%u pixel0=%#x pixel1=%#x pixel2=%#x bad_index=%u bad_value=%#x phase=%u\n",blit,b.submitted,b.completed,b.words,b.polls,b.rptr,b.wptr,b.shadow,b.fence,b.status,b.status3,b.irq,b.fault,b.pixels,b.guards,b.pixel[0],b.pixel[1],b.pixel[2],b.bad_index,b.bad_value,b.phase);
            if(!blit)passed=0;
            if(blit && runtime_mode && !board_runtime_work(&io,&b))passed=0;
        }
    }
    int stopped=marble_cp_halt(&io,&r);
    cp_result=r;
    marble_gpu_log("marble-gpu-cp: MGPU-MARBLE-CP-v2 PASS=%u configured=%u kicked=%u completed=%u words=%u polls=%u rptr=%u wptr=%u shadow=%u fence=%#x marker=%#x expected=0x4d475031 scratch=%#x status=%#x status3=%#x irq=%#x fault=%#x halt_requested=%u halt_observed=%u halt=%#x phase=%u\n",passed && stopped,r.configured,r.kicked,r.completed,r.words,r.polls,r.rptr,r.wptr,r.shadow,r.fence,r.marker,r.scratch,r.status,r.status3,r.irq,r.fault,r.halt_requested,r.halted,r.halt,r.phase);
    return passed && stopped;
}
static int gmu_hfi_work(void *cookie)
{
    MarbleHfiIo io={cookie,board_hfi_read,board_hfi_write,power_delay,board_boot_health};MarbleHfiResult r;
    marble_gpu_log("marble-gmu-step: vendor FW_INIT complete; activating HFI and table ACK test\n");
    int passed=marble_hfi_tables(&io,&hfi_transport,vote_plan.performance,vote_plan.bandwidth,&r);
    for(unsigned i=0;i<2;i++)marble_gpu_log("marble-gmu-hfi: ack index=%u header=%#x returned=%#x\n",i,r.ack_header[i],r.ack_return[i]);
    for(unsigned q=0;q<3;q++)marble_gpu_log("marble-gmu-hfi: queue=%u read=%u write=%u\n",q,hfi_transport.shared[6+12*q+10],hfi_transport.shared[6+12*q+11]);
    marble_gpu_log("marble-gmu-hfi: " MARBLE_GMU_HFI_ABI " PASS=%d ready=%u commands=%u acks=%u failed_stage=%u polls=%u ctrl=%#x fw_result=%#x irq=%#x transport=%u error=%#x notify=%u pending=%#x poisoned=%u skipped=%u\n",passed,r.ready,r.commands,r.acks,r.failed_stage,r.polls,r.ctrl,r.fw_result,r.irq,r.transport,r.error,hfi_notify_attempts,hfi_transport.pending_header,hfi_transport.poisoned,hfi_transport.skipped);
    if(passed) {
        marble_gpu_log("marble-gmu-step: real CORE_FW_START request; GX must remain collapsed\n");
        passed=marble_hfi_core_start(&io,&hfi_transport,&r);
        marble_gpu_log("marble-gmu-core: MGPU-MARBLE-GMU-CORE-v1 PASS=%d commands=%u acks=%u failed_stage=%u polls=%u fw_result=%#x irq=%#x transport=%u error=%#x sent=%#x ack_header=%#x returned=%#x notify=%u pending=%#x poisoned=%u\n",passed,r.commands,r.acks,r.failed_stage,r.polls,r.fw_result,r.irq,r.transport,r.error,r.header,r.ack_header[0],r.ack_return[0],hfi_notify_attempts,hfi_transport.pending_header,hfi_transport.poisoned);
    }
    if(passed && vote_plan.config.gpu_count==8 && vote_plan.config.gpu[1].hz==220000000 &&
        vote_plan.config.bandwidth_count==12) {
        for(unsigned slumber=0;slumber<2 && passed;slumber++) {
            marble_gpu_log("marble-gmu-step: bounded HFI %s; minimum GPU index1 BW index1; no OOB power request\n",slumber?"prepare-slumber":"low-vote");
            passed=marble_hfi_low_vote(&io,&hfi_transport,slumber,&r);
            marble_gpu_log("marble-gmu-vote: MGPU-MARBLE-GMU-LOW-VOTE-v1 slumber=%u PASS=%d commands=%u acks=%u failed_stage=%u polls=%u fw_result=%#x irq=%#x transport=%u error=%#x sent=%#x ack_header=%#x returned=%#x notify=%u pending=%#x poisoned=%u\n",slumber,passed,r.commands,r.acks,r.failed_stage,r.polls,r.fw_result,r.irq,r.transport,r.error,r.header,r.ack_header[0],r.ack_return[0],hfi_notify_attempts,hfi_transport.pending_header,hfi_transport.poisoned);
            if(passed && !slumber) {
                MarbleGxIo gio={cookie,board_gx_read,board_gx_write,power_delay,board_boot_health};
                boot_gx_lease=1;
                marble_gpu_log("marble-gmu-step: real GPU OOB set/probe/clear; no CP commands\n");
                passed=marble_gx_acquire(&gio,&gx_result);
                marble_gpu_log("marble-gmu-gx: MGPU-MARBLE-GMU-GX-v1 PASS=%d requested=%u ack=%u powered=%u intr=%#x gfx=%#x gdscr=%#x rbbm=%#x\n",passed,gx_result.requested,gx_result.acknowledged,gx_result.powered,gx_result.intr,gx_result.gfx_status,gx_result.gdscr,gx_result.rbbm_status);
                if(passed) {
                    for(unsigned i=0;i<5;i++) {
                        uint32_t value=0;
                        if(board_gx_read(cookie,2,marble_gx_saved[i],&value))passed=0;
                        marble_gpu_log("marble-gmu-gx: active offset=%#x value=%#x\n",marble_gx_saved[i],value);
                    }
                }
                if(passed) {
                    passed=marble_hfi_gpu_start(&io,&hfi_transport,gx_result.held,&r);
                    marble_gpu_log("marble-gmu-start: MGPU-MARBLE-GMU-START-v1 PASS=%d held=%u commands=%u acks=%u failed_stage=%u polls=%u fw_result=%#x irq=%#x transport=%u error=%#x sent=%#x ack_header=%#x returned=%#x notify=%u pending=%#x poisoned=%u\n",passed,gx_result.held,r.commands,r.acks,r.failed_stage,r.polls,r.fw_result,r.irq,r.transport,r.error,r.header,r.ack_header[0],r.ack_return[0],hfi_notify_attempts,hfi_transport.pending_header,hfi_transport.poisoned);
                    if(passed)passed=board_cp_test(cookie);
                }
                if(gx_result.requested && !marble_gx_release(&gio,&gx_result))passed=0;
                if(passed && gx_manual_cleanup_test) {
                    marble_gpu_log("marble-gmu-gx: force_stop_test=1 slumber_not_sent=1\n");break;
                }
            }
        }
    } else passed=0;
    return passed?0:-1;
}
static int gmu_sequence_audit(struct boot_device *d)
{
    vm_offset_t pdc=ml_io_map(0x0b290000,0x10000);if(!pdc || tcm_guard(&d->tcm))return 0;
    unsigned match=1;
    for(unsigned i=0;i<MARBLE_SEQUENCE_REGS;i++) {
        const MarbleSequenceRegister *r=marble_sequence_regs+i;
        uint32_t v=power_read((void *)(r->pdc?pdc:d->rsc),r->offset);
        unsigned same=(v&r->mask)==r->value;match&=same;
        marble_gpu_log("marble-gmu-sequence: index=%u pdc=%u offset=%#x value=%#x expected=%#x mask=%#x match=%u\n",i,r->pdc,r->offset,v,r->value,r->mask,same);
    }
    marble_gpu_log("marble-gmu-sequence: prerequisites_match=%u registers=%u writes=0\n",match,(unsigned)MARBLE_SEQUENCE_REGS);
    return match;
}
static int gmu_boot_work(void *cookie)
{
    struct boot_device *d=cookie;
    MarbleBootIo io={d,board_boot_read,board_boot_write,power_delay,board_boot_health,board_gx_stop};MarbleBootResult r;
    MarbleGxIo gio={d,board_gx_read,board_gx_write,power_delay,board_boot_health};
    if(!marble_gx_prepare(&gio,&gx_result)) {
        marble_gpu_log("marble-gmu-gx: prepare refused gdscr=%#x domain=%#x\n",gx_result.before[0],gx_result.before[1]);return -1;
    }
    if(gmu_diagnostic_mode) {
    /* PC=0x41: real M3 read of declared code IOVA and scratch RAM write/read.
     * Not vendor firmware, HFI or GPU execution. Assembled marble-gmu-dma.S. */
    const uint32_t *local_code=marble_fetch_code;
    unsigned external_ready=0;
    if(tcm_guard(&d->tcm))return -2;
    for(unsigned i=0;i<gmu_plan.count;i++)if(gmu_plan.ranges[i].iova==0x100000 &&
        (gmu_plan.ranges[i].permissions&ArmSmmu4KExecute) && gmu_plan.ranges[i].bytes>=sizeof(marble_external_code)) {
        size_t at=(size_t)(gmu_plan.ranges[i].physical-kvtophys((vm_offset_t)gmu_ram));
        if(at>sizeof(gmu_ram)-sizeof(marble_external_code))return -2;
        memcpy(gmu_ram+at,marble_external_code,sizeof(marble_external_code));
        uint64_t ctr;__asm__ volatile("mrs %0, ctr_el0":"=r"(ctr));size_t line=(size_t)4<<((ctr>>16)&15);
        vm_offset_t start=(vm_offset_t)gmu_ram+at;
        for(vm_offset_t va=start&~(line-1);va<start+sizeof(marble_external_code);va+=line)
            __asm__ volatile("dc cvac, %0"::"r"(va):"memory");
        __asm__ volatile("dsb sy":::"memory");external_ready=1;
    }
    if(!external_ready)return -2;
    if(tcm_guard(&d->tcm) || tcm_write(&d->tcm,0x5004,0x41))return -2;
    for(unsigned i=0;i<4;i++)if(tcm_write(&d->tcm,0x9000+i*4,0))return -2;
    for(unsigned i=0;i<sizeof(marble_fetch_code)/sizeof(marble_fetch_code[0]);i++)if(tcm_write(&d->tcm,0x5040+i*4,local_code[i]))return -2;
    tcm_barrier(&d->tcm);
    for(unsigned i=0;i<sizeof(marble_fetch_code)/sizeof(marble_fetch_code[0]);i++){uint32_t v;if(tcm_read(&d->tcm,0x5040+i*4,&v) || v!=local_code[i])return -2;}
    for(unsigned i=0;i<4;i++){uint32_t v;if(tcm_read(&d->tcm,0x9000+i*4,&v) || v)return -2;}
    marble_gpu_log("marble-gmu-step: M3 real external instruction fetch diagnostic; NOT FW_INIT success\n");
    } else marble_gpu_log("marble-gmu-step: vendor firmware cold start; exact SID5+SID405 lease\n");
    if(!gmu_diagnostic_mode)gmu_sequence_audit(d);
    /* All cached accesses/cleaning finished while reset is asserted. During
     * the live exchange ONLY this uncached alias owns CPU queue accesses. */
    if(!gmu_diagnostic_mode) {
        vm_offset_t uncached=ml_io_map_wcomb(kvtophys((vm_offset_t)gmu_hfi),sizeof(gmu_hfi));
        if(!uncached)return -1;
        uint64_t ctr;__asm__ volatile("mrs %0, ctr_el0":"=r"(ctr));size_t line=(size_t)4<<((ctr>>16)&15);
        for(vm_offset_t va=(vm_offset_t)gmu_hfi;va<(vm_offset_t)gmu_hfi+sizeof(gmu_hfi);va+=line)
            __asm__ volatile("dc civac, %0"::"r"(va):"memory");
        __asm__ volatile("dsb sy":::"memory");
        hfi_transport.shared=(volatile uint32_t *)uncached;
        hfi_transport.cookie=d;hfi_transport.notify=board_hfi_notify;
    }
    if(!gmu_diagnostic_mode && (!gpu_inactive_memory() || (runtime_mode?
        (!runtime_tables_prepare() || !marble_gpu_smmu_live_begin(d->smmu,&runtime_arena,&runtime_table_state,
            gpu_ranges,5,board_runtime_quiesce,d)):
        !marble_gpu_smmu_begin(d->smmu,&gpu_config,&gpu_table_result,gpu_tables,gpu_ranges,5,MARBLE_GPU_TABLE_PAGES)))) {
        if(!marble_gpu_smmu_end(d->tcm.gmu,d->tcm.cc))return -2;
        return -1;
    }
    int passed=marble_boot_transaction_work(&io,&r,1,gmu_diagnostic_mode?NULL:gmu_hfi_work,d);
    if(!gmu_diagnostic_mode && (r.retain || !marble_gpu_smmu_end(d->tcm.gmu,d->tcm.cc)))return -2;
    int local_ok=passed;
    if(gmu_diagnostic_mode) {
    if(!r.retain)for(unsigned i=0;i<4;i++)tcm_read(&d->tcm,0x9000+i*4,boot_dma_values+i);
    uint32_t expected=marble_tcm_word(gmu_ram);
    local_ok=r.configured && r.reset_released && r.stopped && r.restored && !r.retain &&
        boot_dma_values[0]==0x4d475046 && boot_dma_values[1]==expected &&
        boot_dma_values[2]==0xdec0de01 && boot_dma_values[3]==0x4d475044;
    marble_gpu_log("marble-gmu-fetch: MGPU-MARBLE-GMU-FETCH-v1 PASS=%d progress=%#x read=%#x expected=%#x written=%#x done=%#x vendor_firmware=0\n",local_ok,boot_dma_values[0],boot_dma_values[1],expected,boot_dma_values[2],boot_dma_values[3]);
    }
    for(unsigned i=0;i<MARBLE_BOOT_REGS;i++)marble_gpu_log("marble-gmu-boot: register block=%u offset=%#x before=%#x after=%#x\n",marble_boot_blocks[i],marble_boot_regs[i],r.before[i],r.after[i]);
    for(unsigned i=0;i<4;i++)marble_gpu_log("marble-gmu-boot: RSC index=%u before=%#x after=%#x\n",i,r.rsc_before[i],r.rsc_after[i]);
    for(unsigned i=0;i<2;i++)marble_gpu_log("marble-gmu-boot: IRQ index=%u before=%#x after=%#x\n",i,r.irq_before[i],r.irq_after[i]);
    marble_gpu_log("marble-gmu-boot: " MARBLE_GMU_BOOT_ABI " PASS=%d configured=%u reset_released=%u initialized=%u fw_result=%#x stopped=%u ack=%#x restored=%u retain=%u refused=%u phase=%u\n",passed,r.configured,r.reset_released,r.initialized,r.fw_result,r.stopped,r.last_ack,r.restored,r.retain,r.refused,r.phase);
    return r.retain?-2:local_ok?0:-1;
}
static int gmu_tcm_work(void *cookie)
{
    struct boot_device *d=cookie;
    MarbleTcmIo io={&d->tcm,tcm_read,tcm_write,tcm_barrier,tcm_guard};MarbleTcmResult r;
    boot_tcm_lease=1;
    int passed=marble_tcm_transaction_work(&io,&tcm_plan,files[0].data,files[0].bytes,17,tcm_snapshot,&r,gmu_boot_work,d);
    boot_tcm_lease=0;
    marble_gpu_log("marble-gmu-boot: TCM PASS=%d stable=%u loaded=%u restored=%u retain=%u reads=%u writes=%u phase=%u\n",passed,r.stable,r.loaded,r.restored,r.retain,r.reads,r.writes,r.phase);
    return r.retain?-2:passed?0:-1;
}
static int gmu_cold_test(void *cookie)
{
    struct boot_device *d=cookie;
    struct clock_device clock={&d->tcm,ml_io_map(0x00100000,0x80000)};if(!clock.gcc)return -1;
    MarbleClockIo io={&clock,board_clock_read,board_clock_write,power_delay,board_clock_guard};MarbleClockResult r;
    int passed=marble_clock_transaction_work(&io,&r,gmu_tcm_work,d);
    marble_gpu_log("marble-gmu-boot: clock_lease PASS=%d hz_gmu=%u hz_hub=%u restored=%u retain=%u\n",passed,r.hz[0],r.hz[1],r.restored,r.retain);
    if(r.retain)panic("marble GMU live master or cleanup uncertain; retain clocks/SMMU/CX/RAM, reset required");
    /* GMU DMA may have written log/RAM. Make CPU observations see memory. */
    uint64_t ctr;__asm__ volatile("mrs %0, ctr_el0":"=r"(ctr));size_t line=(size_t)4<<((ctr>>16)&15);
    for(unsigned j=0;j<3;j++) {
        vm_offset_t start=(vm_offset_t)(j==0?gmu_ram:j==1?(uint8_t *)gmu_log:(uint8_t *)gmu_hfi);
        size_t bytes=j==0?sizeof(gmu_ram):0x4000;
        for(vm_offset_t va=start;va<start+bytes;va+=line)__asm__ volatile("dc ivac, %0"::"r"(va):"memory");
    }
    __asm__ volatile("dsb sy":::"memory");
    if(gmu_diagnostic_mode) {
    unsigned dma_ram=0;
    for(unsigned i=0;i<gmu_plan.count;i++)if(gmu_plan.ranges[i].iova==0x60000000) {
        size_t at=(size_t)(gmu_plan.ranges[i].physical-kvtophys((vm_offset_t)gmu_ram));
        if(at<=sizeof(gmu_ram)-4)dma_ram=marble_tcm_word(gmu_ram+at)==0xdec0de01;
    }
    marble_gpu_log("marble-gmu-dma: CPU_after_invalidate=%u expected=0xdec0de01\n",dma_ram);
    if(!dma_ram)passed=0;
    }
    return passed?0:-1;
}
static int gmu_prerequisites(vm_offset_t cc)
{
    struct tcm_device device={ml_io_map(0x03d68000,0x37000),cc};
    if(!device.gmu)return -1;
    MarbleGmuClockIo io={&device,clock_read,tcm_guard};MarbleGmuClockResult r;
    int passed=marble_gmu_clock_snapshot(&io,&r);
    for(unsigned i=0;i<MARBLE_GMU_CLOCK_REGS;i++)
        marble_gpu_log("marble-gmu-preflight: clock offset=%#x value=%#x\n",marble_gmu_clock_offsets[i],r.value[i]);
    marble_gpu_log("marble-gmu-preflight: clocks PASS=%d stable=%u guarded=%u reads=%u writes=0 GMU_nominal=%u HUB_nominal=%u AHB_div=%u CX_div=%u; table rates, not measured\n",
        passed,r.stable,r.guarded,r.reads,marble_gmu_clock_nominal(0,r.value[1]),
        marble_gmu_clock_nominal(1,r.value[3]),(r.value[4]&15)+1,(r.value[5]&15)+1);
    if(!passed)return -1;
    /* DT's no-map AOP command database is firmware RAM, not device registers.
     * Use the existing uncached memory mapping API and keep our own snapshot.
     * Never touch the RPMh RSC, AOP mailbox, rail votes or RCG update bits. */
    vm_offset_t va=ml_io_map_wcomb(MARBLE_CMD_DB_PHYS,MARBLE_CMD_DB_BYTES);
    if(!va)return -1;
    const volatile uint32_t *source=(const volatile uint32_t *)va;
    for(unsigned i=0;i<MARBLE_CMD_DB_BYTES/4;i++) {
        uint32_t word=source[i];memcpy(cmd_db_snapshot+i*4,&word,4);
    }
    __asm__ volatile("dsb sy":::"memory");
    unsigned stable=1;
    for(unsigned i=0;i<MARBLE_CMD_DB_BYTES/4;i++) {
        uint32_t word=source[i],before;memcpy(&before,cmd_db_snapshot+i*4,4);
        if(word!=before)stable=0;
    }
    SHA256_CTX ctx;uint8_t digest[32];char hex[65];
    SHA256_Init(&ctx);SHA256_Update(&ctx,cmd_db_snapshot,sizeof(cmd_db_snapshot));SHA256_Final(digest,&ctx);
    for(unsigned i=0;i<32;i++){hex[2*i]="0123456789abcdef"[digest[i]>>4];hex[2*i+1]="0123456789abcdef"[digest[i]&15];}hex[64]=0;
    marble_gpu_log("marble-gmu-preflight: cmd_db PA=%#llx bytes=%u stable=%u reads=65536 writes=0 version=%#x standalone=%u sha256=%s\n",
        MARBLE_CMD_DB_PHYS,MARBLE_CMD_DB_BYTES,stable,marble_db_u32(cmd_db_snapshot),marble_db_u32(cmd_db_snapshot+140)&1,hex);
    if(!stable || tcm_guard(&device))return -1;
    static const char *const names[]={"gfx.lvl","cx.lvl","mx.lvl","mxc.lvl","CN0","SH0","MC0","ACV"};
    for(unsigned i=0;i<sizeof(names)/sizeof(names[0]);i++) {
        MarbleCmdDbEntry entry;MarbleCmdDbIssue issue;
        int found=marble_cmd_db_find_diagnostic(cmd_db_snapshot,sizeof(cmd_db_snapshot),names[i],&entry,&issue);
        marble_gpu_log("marble-gmu-preflight: resource name=%s found=%d\n",names[i],found);
        if(!found) {
            marble_gpu_log("marble-gmu-preflight: DB refusal code=%u slot=%u entry=%u\n",issue.code,issue.slot,issue.entry);
            for(unsigned s=0;s<8;s++) {
                const uint8_t *h=cmd_db_snapshot+8+16*s;
                marble_gpu_log("marble-gmu-preflight: directory slot=%u slave=%u headers=%u data=%u count=%u version=%#x\n",
                    s,marble_db_u16(h),marble_db_u16(h+2),marble_db_u16(h+4),marble_db_u16(h+6),marble_db_u16(h+8));
                size_t offset=(size_t)marble_db_u16(h+2)+(size_t)issue.entry*24;
                if(s==issue.slot && marble_db_span(offset,24,sizeof(cmd_db_snapshot)-MARBLE_CMD_DB_HEADER)) {
                    const uint8_t *e=cmd_db_snapshot+MARBLE_CMD_DB_HEADER+offset;
                    marble_gpu_log("marble-gmu-preflight: refused_entry name=%.8s address=%#x bytes=%u aux_offset=%u\n",
                        e,marble_db_u32(e+16),marble_db_u16(e+20),marble_db_u16(e+22));
                }
            }
            return -1;
        }
        if(entry.slave!=(i<4?3:5) || !entry.bytes || entry.bytes>64)return -1;
        char aux[129];
        for(unsigned j=0;j<entry.bytes;j++){aux[2*j]="0123456789abcdef"[entry.aux[j]>>4];aux[2*j+1]="0123456789abcdef"[entry.aux[j]&15];}aux[2*entry.bytes]=0;
        marble_gpu_log("marble-gmu-preflight: entry name=%s slave=%u version=%#x address=%#x bytes=%u aux=%s\n",
            names[i],entry.slave,entry.version,entry.address,entry.bytes,aux);
        if(i>=4) {
            AdrenoHfiBcm bcm={0};bcm.address=entry.address;
            int status=AdrenoHfiBcmAux(&bcm,entry.aux,entry.bytes);
            marble_gpu_log("marble-gmu-preflight: BCM name=%s status=%d unit=%u width=%u vcd=%u; decoded only\n",
                names[i],status,bcm.unit,bcm.width,bcm.vcd);
            if(status)return -1;
        }
    }
    marble_gpu_log("marble-gmu-preflight: " MARBLE_GMU_PREFLIGHT_ABI " PASS=1 clocks_writes=0 db_writes=0 votes=0 notifications=0 reset_released=0\n");
    int status=probe_stage>=13?gmu_votes(&device):0;
    if(!status && probe_stage>=14)status=gmu_clocks(&device);
    if(!status && probe_stage>=15)status=gmu_rpmh(&device);
    if(!status && probe_stage>=16)status=gmu_dependencies(&device);
    return status;
}
static int tcm_probe(vm_offset_t cc)
{
    struct tcm_device device={ml_io_map(0x03d68000,0x37000),cc};
    if(!device.gmu)return -1;
    MarbleTcmIo io={&device,tcm_read,tcm_write,tcm_barrier,tcm_guard};MarbleTcmResult r;
    unsigned mode=probe_stage>=11?9:probe_stage; /* resource/preflight stages have NO TCM writes */
    marble_gpu_log("marble-gmu-tcm: " MARBLE_GMU_TCM_ABI " stage=%u spans=%u words=%u; GMU held reset\n",
        mode,tcm_plan.count,tcm_plan.words);
    int passed=marble_tcm_transaction(&io,&tcm_plan,files[0].data,files[0].bytes,mode,tcm_snapshot,&r);
    marble_gpu_log("marble-gmu-tcm: PASS=%d stable=%u loaded=%u restored=%u retain=%u reads=%u writes=%u attempted=%u phase=%u failure_offset=%#x\n",
        passed,r.stable,r.loaded,r.restored,r.retain,r.reads,r.writes,r.attempted,r.phase,r.failure_offset);
    if(r.stable) {
        SHA256_CTX ctx;uint8_t digest[32];char hex[65];
        SHA256_Init(&ctx);SHA256_Update(&ctx,tcm_snapshot,sizeof(tcm_snapshot));SHA256_Final(digest,&ctx);
        for(unsigned i=0;i<32;i++) {hex[2*i]="0123456789abcdef"[digest[i]>>4];hex[2*i+1]="0123456789abcdef"[digest[i]&15];}
        hex[64]=0;
        marble_gpu_log("marble-gmu-tcm: original_sha256=%s ITCM_first=%#x ITCM_last=%#x DTCM_first=%#x DTCM_last=%#x\n",
            hex,tcm_snapshot[0],tcm_snapshot[4095],tcm_snapshot[4096],tcm_snapshot[8191]);
    }
    if(r.retain)panic("marble TCM cleanup uncertain; retain RAM, cold reset required");
    return passed?0:-1;
}
static int smmu_observe(void *cookie)
{
    vm_offset_t gcc=ml_io_map(0x00181000,0x1000);
    if(!gcc)return -1;
    uint32_t mem=read_status(gcc,0x10,"GCC_GPU_MEMNOC_GFX_POWERED");
    uint32_t dvm=read_status(gcc,0x18,"GCC_GPU_SNOC_DVM_GFX_POWERED");
    if((mem|dvm)&0x80000000u) {
        marble_gpu_log("marble-gpu: SMMU observation SKIPPED: parent clock is halted\n");return -1;
    }
    vm_offset_t smmu=ml_io_map(0x03da0000,probe_stage>=4?0x40000:0x1000);
    if(!smmu)return -1;
    read_status(smmu,0,"SMMU_SCR0");uint32_t id0=read_status(smmu,0x20,"SMMU_IDR0");
    uint32_t id1=read_status(smmu,0x24,"SMMU_IDR1");
    uint32_t id2=read_status(smmu,0x28,"SMMU_IDR2");
    read_status(smmu,0x48,"SMMU_GFSR");
    if(id0!=0x4c017e18 || id1!=0x40000016 || id2!=0x5511) {
        marble_gpu_log("marble-gpu: SMMU stream audit SKIPPED: identity differs\n");return -1;
    }
    marble_gpu_log("marble-gpu: SMMU native4K=%u OAS-encoding=%u contexts=%u stream-slots=%u; read-only audit\n",
        (id2>>12)&1,(id2>>4)&15,id1&255,id0&255);
    for(unsigned i=0;i<(id0&255);i++) {
        uint32_t smr=power_read((void *)smmu,0x800+4*i);
        uint32_t route=power_read((void *)smmu,0xc00+4*i);
        marble_gpu_log("marble-gpu: SMMU slot=%u SMR=%#x S2CR=%#x\n",i,smr,route);
    }
    ArmSmmu4KContextConfig config={&offline_config,NULL,&offline_result,NULL,id0,id2,0,1,0,0};
    ArmSmmu4KContextImage image;
    ArmSmmu4KStatus status=ArmSmmu4KEncodeContext(&config,&image);
    marble_gpu_log("marble-gpu: inactive context encode status=%u; no bank/stream/ASID assigned\n",status);
    if(status!=ArmSmmu4KSuccess)return -1;
    marble_gpu_log("marble-gpu: inactive context TCR=%#x TCR2=%#x TTBR0=%#llx MAIR=%#x; NOT programmed\n",
        image.tcr,image.tcr2,image.ttbr0,image.mair0);
    if(probe_stage>=4) {
        struct boot_device d={{ml_io_map(0x03d68000,0x37000),(vm_offset_t)cookie},
            ml_io_map(0x03d00000,0x40000),ml_io_map(0x03d9e000,0x1000),ml_io_map(0x03d50000,0x10000),smmu};
        if(probe_stage==17 && (!d.tcm.gmu || !d.gpu || !d.misc || !d.rsc ||
            tcm_probe((vm_offset_t)cookie) || gmu_prerequisites((vm_offset_t)cookie)))return -1;
        int result=marble_smmu_probe(smmu,probe_stage,&offline_config,&offline_result,offline_tables,
            offline_ranges,offline_range_count,offline_table_capacity,probe_stage==17?gmu_cold_test:NULL,&d);
        /* TCM access starts only after the CB/stream lease has been released. */
        if(!result && probe_stage>=9 && probe_stage<17)result=tcm_probe((vm_offset_t)cookie);
        if(!result && probe_stage>=12 && probe_stage<17)result=gmu_prerequisites((vm_offset_t)cookie);
        return result;
    }
    return 0;
}

/* Returns true for marble even with diagnostics disabled, so the unrelated
 * Adreno642L startup path cannot run on this board. */
static void marble_power_work(void *cookie)
{
    struct marble_gpu_power_io io={cookie,power_read,power_write,power_delay,smmu_observe};
    struct marble_gpu_power_result r;
    marble_gpu_power_transaction(&io,&r);
    marble_gpu_log("marble-gpu: CX result powered=%u clocks=%u observed=%u restored=%u skipped=%u\n",r.cx_on,r.clocks_on,r.observed,r.restored,r.skipped);
    marble_gpu_log("marble-gpu: CX before=%#x status=%#x after=%#x status=%#x AHB=%#x SMMU=%#x\n",r.cx_before,r.cx_status_before,r.cx_after,r.cx_status_after,r.ahb_after,r.smmu_after);
    if(!r.restored || !r.observed)runtime_failed=1;
}
static void marble_runtime_thread(void *argument,wait_result_t wait_result)
{
    (void)argument;(void)wait_result;
    if(!__atomic_load_n(&runtime_stop_requested,__ATOMIC_ACQUIRE))marble_power_work((void *)runtime_cc);
    marble_gpu_log("marble-gpu-runtime: released PASS=%u draws=%u wraps=%u active=%u\n",!runtime_failed,runtime_draws,runtime_wraps,runtime_active);
    __atomic_store_n(&runtime_inflight,0,__ATOMIC_RELEASE);
    thread_terminate(current_thread());
}
bool marble_adreno_init(void);
bool marble_adreno_init(void)
{
    DTEntry entry;const void *prop;unsigned int size;uint32_t probe=0;
    if(SecureDTLookupEntry(NULL,"/arm-io/marble-gpu",&entry)!=kSuccess)return false;
    __atomic_store_n(&marble_present,1,__ATOMIC_RELEASE);
    if(!PE_parse_boot_argn("marble_gpu_probe",&probe,sizeof(probe)) || !probe)return true;
    if(probe>17 || SecureDTGetProperty(entry,"chip-id",&prop,&size)!=kSuccess || size!=4 ||
        *(const uint32_t *)prop!=MARBLE_GPU_CHIP_ID) {
        marble_gpu_log("marble-gpu: board profile refused\n");return true;
    }
    if(SecureDTGetProperty(entry,"windows",&prop,&size)!=kSuccess || size!=sizeof(marble_gpu_windows) ||
        memcmp(prop,marble_gpu_windows,sizeof(marble_gpu_windows))) {
        marble_gpu_log("marble-gpu: board windows refused\n");return true;
    }
    if(probe>=6 && (SecureDTGetProperty(entry,"tbu-windows",&prop,&size)!=kSuccess ||
        size!=sizeof(marble_tbu_windows) || memcmp(prop,marble_tbu_windows,sizeof(marble_tbu_windows)))) {
        marble_gpu_log("marble-gpu: TBU windows refused\n");return true;
    }
    const uint64_t db_window[]={MARBLE_CMD_DB_PHYS,MARBLE_CMD_DB_BYTES};
    if(probe>=12 && (SecureDTGetProperty(entry,"cmd-db-window",&prop,&size)!=kSuccess ||
        size!=sizeof(db_window) || memcmp(prop,db_window,sizeof(db_window)))) {
        marble_gpu_log("marble-gpu: command DB window refused\n");return true;
    }
    const uint64_t rpmh_window[]={MARBLE_RPMH_PHYS,0x100};
    const uint64_t boot_window[]={0x03d9e000,0x1000};
    if(probe>=17 && (SecureDTGetProperty(entry,"gmu-boot-window",&prop,&size)!=kSuccess ||
        size!=sizeof(boot_window) || memcmp(prop,boot_window,sizeof(boot_window))))return true;
    if(probe>=15 && (SecureDTGetProperty(entry,"rpmh-window",&prop,&size)!=kSuccess ||
        size!=sizeof(rpmh_window) || memcmp(prop,rpmh_window,sizeof(rpmh_window)))) {
        marble_gpu_log("marble-gpu: RPMh window refused\n");return true;
    }
    marble_gpu_log("marble-gpu: " MARBLE_GPU_ABI " chip=%#x stage=%u\n",MARBLE_GPU_CHIP_ID,probe);
    if(!firmware_preflight() || !inactive_tables(probe) ||
        (probe>=9 && !marble_tcm_plan(firmware_blocks,firmware_block_count,files[0].bytes,&tcm_plan))) {
        marble_gpu_log("marble-gpu: preflight FAILED; hardware untouched\n");return true;
    }
    vm_offset_t cc=probe>=2?clock_preflight():0;
    probe_stage=probe;
    PE_parse_boot_argn("marble_gpu_runtime",&runtime_mode,sizeof(runtime_mode));
    if(runtime_mode) {
        if(runtime_mode!=1 || probe!=17 || !cc) {
            marble_gpu_log("marble-gpu-runtime: profile refused\n");return true;
        }
        runtime_cc=cc;
        __atomic_store_n(&runtime_inflight,1,__ATOMIC_RELEASE);
        thread_t worker;
        if(kernel_thread_start(marble_runtime_thread,NULL,&worker)!=KERN_SUCCESS) {
            __atomic_store_n(&runtime_inflight,0,__ATOMIC_RELEASE);
            marble_gpu_log("marble-gpu-runtime: worker creation failed\n");
        } else thread_deallocate(worker);
        return true;
    }
    if(probe>=3 && cc) {
        /* Also guard synchronous diagnostics against a panic's loader return. */
        __atomic_store_n(&runtime_inflight,1,__ATOMIC_RELEASE);
        marble_gpu_log("marble-gpu: " MARBLE_GPU_POWER_ABI " begin; CX/local clocks only\n");
        struct marble_gpu_power_io io={(void *)cc,power_read,power_write,power_delay,smmu_observe};
        struct marble_gpu_power_result r;
        marble_gpu_power_transaction(&io,&r);
        marble_gpu_log("marble-gpu: CX result powered=%u clocks=%u observed=%u restored=%u skipped=%u\n",
            r.cx_on,r.clocks_on,r.observed,r.restored,r.skipped);
        marble_gpu_log("marble-gpu: CX before=%#x status=%#x after=%#x status=%#x AHB=%#x SMMU=%#x\n",
            r.cx_before,r.cx_status_before,r.cx_after,r.cx_status_after,r.ahb_after,r.smmu_after);
        if(probe>=8) {
            uint8_t digest[32];SHA256_CTX ctx;
            SHA256_Init(&ctx);SHA256_Update(&ctx,gmu_ram,sizeof(gmu_ram));SHA256_Final(digest,&ctx);
            if(probe<17)marble_gpu_log("marble-gmu-memory: RAM_image_intact=%d; GMU not executed\n",!memcmp(digest,gmu_ram_sha,sizeof(digest)));
            else marble_gpu_log("marble-gmu-memory: RAM_image_intact=%d; execution_allowed=1\n",!memcmp(digest,gmu_ram_sha,sizeof(digest)));
        }
        if(probe>=11) {
            uint8_t digest[32];resources_digest(digest);
            int intact=!memcmp(digest,resource_sha,sizeof(digest));
            int valid=marble_hfi_image_valid(gmu_hfi,sizeof(gmu_hfi),resource_plan.hfi_iova);
            marble_gpu_log(probe<17?"marble-gmu-resources: images_intact=%d layout_ok=%d notify=%u pending=%u sequence=%u poisoned=%u; GMU not executed\n":"marble-gmu-resources: images_intact=%d layout_ok=%d notify=%u pending=%u sequence=%u poisoned=%u; execution_allowed=1\n",
                intact,valid,hfi_notify_attempts,hfi_transport.pending_header,hfi_transport.sequence,hfi_transport.poisoned);
        }
        __atomic_store_n(&runtime_inflight,0,__ATOMIC_RELEASE);
    }
    marble_gpu_log("marble-gpu: probe complete; %s, acceleration disabled\n",probe>=17?"GMU execution test requested":"GMU not started");
    return true;
}
