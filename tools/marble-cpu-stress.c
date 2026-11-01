/* Bounded arithmetic/memory and timer test. Uses ordinary userspace memory;
 * never changes affinity, clocks, voltage, devices or storage. */
#include <mach/mach_time.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/resource.h>
#include <sys/sysctl.h>
#include <unistd.h>

#define WORKERS 4
#define WORDS (128*1024)
#define ROUNDS 1000000ULL
static volatile unsigned stop;
static uint64_t completed[WORKERS], failures[WORKERS];
static uint64_t ticks(void) { uint64_t t; __asm__ volatile("mrs %0, cntvct_el0":"=r"(t)); return t; }
static uint64_t ns(void) {
    static mach_timebase_info_data_t tb;
    if (!tb.denom) mach_timebase_info(&tb);
    uint64_t t=mach_absolute_time();
    return t/tb.denom*tb.numer+(t%tb.denom)*tb.numer/tb.denom;
}
static void *worker(void *arg) {
    unsigned id=(unsigned)(uintptr_t)arg;
    uint64_t *mem=malloc(WORDS*sizeof(*mem));
    if (!mem) { failures[id]++; return NULL; }
    while (!__atomic_load_n(&stop,__ATOMIC_RELAXED)) {
        volatile uint64_t sum=0;
        for (uint64_t i=0;i<ROUNDS;i++) sum+=i*i;
        if (sum!=(ROUNDS-1)*ROUNDS*(2*ROUNDS-1)/6) failures[id]++;
        uint64_t salt=completed[id]+((uint64_t)id<<48);
        for (unsigned i=0;i<WORDS;i++) mem[i]=salt^((uint64_t)i*0x9e3779b97f4a7c15ULL);
        __asm__ volatile("":::"memory");
        for (unsigned i=0;i<WORDS;i++) if(mem[i]!=(salt^((uint64_t)i*0x9e3779b97f4a7c15ULL))) failures[id]++;
        completed[id]++;
    }
    free(mem); return NULL;
}
int main(int argc,char **argv) {
    unsigned seconds=argc>1?(unsigned)atoi(argv[1]):60;
    if(seconds<1 || seconds>300) return 2;
    pthread_t threads[WORKERS];
    int ncpu=0; size_t size=sizeof(ncpu);
    sysctlbyname("hw.activecpu",&ncpu,&size,NULL,0);
    uint64_t before=ns(), counter=ticks(), prev=before;
    unsigned started=0, timer_errors=0;
    for(;started<WORKERS;started++) if(pthread_create(&threads[started],NULL,worker,(void *)(uintptr_t)started)) break;
    for(unsigned i=0;i<seconds;i++) {
        sleep(1);
        uint64_t now=ns(); if(now<=prev) timer_errors++; prev=now;
    }
    __atomic_store_n(&stop,1,__ATOMIC_RELAXED);
    for(unsigned i=0;i<started;i++) pthread_join(threads[i],NULL);
    uint64_t elapsed=ns()-before, source=(ticks()-counter)*1000000000ULL/19200000ULL;
    uint64_t drift=elapsed>source?elapsed-source:source-elapsed;
    struct rusage usage; getrusage(RUSAGE_SELF,&usage);
    uint64_t cpu_us=(uint64_t)(usage.ru_utime.tv_sec+usage.ru_stime.tv_sec)*1000000+
                    usage.ru_utime.tv_usec+usage.ru_stime.tv_usec;
    unsigned errors=timer_errors+(started!=WORKERS)+(drift>elapsed/100);
    for(unsigned i=0;i<WORKERS;i++) {
        printf("marble-cpu-stress: worker=%u batches=%llu failures=%llu\n",i,completed[i],failures[i]);
        errors+=!!failures[i]+!completed[i];
    }
    printf("marble-cpu-stress: %s activecpu=%d threads=%u wall_ms=%llu cpu_ms=%llu timer_drift_us=%llu errors=%u; frequency unchanged; no temperature sensor\n",
           errors?"FAIL":"PASS",ncpu,started,elapsed/1000000,cpu_us/1000,drift/1000,errors);
    return errors?1:0;
}
