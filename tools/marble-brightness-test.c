/* Static RAM-only integration test: /dev/rmd0 is this temporary boot's RAM
 * image, never a UFS block device. No iOS/GPU and no Android filesystem. */
#include "mini.h"
#include "../xnuloader/marble_brightness_mailbox.h"
static int disk=-1;
static int read_box(MarbleBrightnessMailbox *b)
{return pread(disk,b,sizeof(*b),MB_OFFSET)==sizeof(*b);}
static int request(uint64_t sequence,unsigned milli,unsigned reserved)
{
    uint64_t odd=sequence-1;
    struct {uint32_t milli,reserved;} p={milli,reserved};
    return pwrite(disk,&odd,8,MB_OFFSET+64)==8 &&
        pwrite(disk,&p,8,MB_OFFSET+72)==8 &&
        pwrite(disk,&sequence,8,MB_OFFSET+64)==8;
}
static int rejection(uint64_t seq,unsigned milli,unsigned reserved)
{
    MarbleBrightnessMailbox b;
    if(!request(seq,milli,reserved) || usleep(600000) || !read_box(&b))return 0;
    return !b.acknowledged && !b.failed;
}
int main(int argc,char **argv,char **envp)
{
    (void)argc;(void)argv;(void)envp;
    int console=open("/dev/console",O_RDWR,0);
    if(console>=0){dup2(console,0);dup2(console,1);dup2(console,2);if(console>2)close(console);}
    disk=open("/dev/rmd0",O_RDWR,0);
    struct timeval now;
    if(gettimeofday(&now,0)==0)printf("marble-brightness-test: calendar_seconds=%llu\n",(uint64_t)now.tv_sec);
    MarbleBrightnessMailbox b;int passed=0;
    if(disk<0 || !read_box(&b) || b.magic!=MB_MAGIC || b.version!=1 || b.ready!=1 || b.failed)goto end;
    printf("marble-brightness-test: ready original=%u minimum=%u maximum=%u GPU=0 RAM_only=1\n",b.original,b.minimum,b.maximum);
    /* Invalid publication, out-of-range payload, reserved bits. No panel write
     * or acknowledgement may occur for any of these requests. */
    if(!rejection(1,500,0) || !rejection(2,1001,0) || !rejection(4,500,1))goto end;
    printf("marble-brightness-test: malformed_rejected PASS=1\n");
    const unsigned levels[]={250,750,1000};
    for(unsigned i=0;i<3;i++) {
        uint64_t seq=6+i*2;
        if(!request(seq,levels[i],0))goto end;
        unsigned tries=0;
        do {if(usleep(50000) || !read_box(&b))goto end;}while(b.acknowledged!=seq && ++tries<30);
        unsigned target=marble_brightness_target(b.original,b.minimum,levels[i]);
        if(b.failed || b.acknowledged!=seq || b.result!=1 || b.accepted_milli!=levels[i] || b.actual!=target)goto end;
        printf("marble-brightness-test: seq=%llu milli=%u expected=%u actual=%u PASS=1\n",seq,levels[i],target,b.actual);
    }
    passed=1;
    /* Keep a bounded quiet observation interval so the independent firmware
     * monitor records at least five one-second runtime samples. */
    if(usleep(6000000))passed=0;
end:
    printf("marble-brightness-test: complete PASS=%u errno=%d\n",passed,errno);
    if(disk>=0)close(disk);
    reboot(0x08);
    for(;;)usleep(200000);
}
