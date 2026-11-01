/* Static PID1 exercises the existing kernel audio device with synthetic
 * samples only. Queue probe is RAM-only; no DSP, codec or speaker writes. */
#include "mini.h"
#include "../xnu/files/bsd/sys/marble_audio_queue.h"
static int audio=-1;
static int16_t source[2048];
static MarbleAudioQueueTransfer r;
static int pop(unsigned count)
{
    memset(&r,0,sizeof(r));r.magic=MAQ_MAGIC;r.version=1;r.requested=count;
    return mini_syscall(SYS_ioctl,audio,MAQ_IOCTL,(long)&r,0,0,0)==0 && r.magic==MAQ_MAGIC && r.version==1 && r.capacity==4096 && !r.failed;
}
static int send(unsigned base)
{
    for(unsigned i=0;i<1024;i++) {source[2*i]=(int16_t)(base+i);source[2*i+1]=(int16_t)~(base+i);}
    return write(audio,source,sizeof(source))==sizeof(source);
}
static int verify(unsigned base)
{
    for(unsigned i=0;i<256;i++)if(r.samples[2*i]!=(int16_t)(base+i) || r.samples[2*i+1]!=(int16_t)~(base+i))return 0;
    return r.actual==256;
}
int main(int argc,char **argv,char **envp)
{
    (void)argc;(void)argv;(void)envp;
    int console=open("/dev/console",O_RDWR,0);
    if(console>=0) {dup2(console,0);dup2(console,1);dup2(console,2);if(console>2)close(console);}
    unsigned passed=0;
    audio=open("/dev/redwood-audio",O_WRONLY,0);
    if(audio<0 || !pop(0) || r.accepted || r.queued)goto end;
    r.requested=257;
    if(mini_syscall(SYS_ioctl,audio,MAQ_IOCTL,(long)&r,0,0,0)!=-1 || errno!=22)goto end;
    if(!pop(0) || r.accepted || r.queued)goto end;
    printf("marble-audio-queue-test: invalid_count_rejected=1 DSP=0\n");
    for(unsigned i=0;i<6;i++)if(!send(i*1024))goto end;
    if(!pop(0) || r.queued!=4096 || r.accepted!=4096 || r.dropped!=2048)goto end;
    for(unsigned i=0;i<16;i++)if(!pop(256) || !verify(i*256))goto end;
    if(r.consumed!=4096 || r.queued)goto end;
    if(!pop(256) || r.actual || r.silence!=256)goto end;
    for(unsigned i=0;i<512;i++)if(r.samples[i])goto end;
    printf("marble-audio-queue-test: full_order_and_underflow PASS=1 accepted=4096 dropped=2048 silence=256\n");
    for(unsigned block=0;block<128;block++) {
        if(!send(8192+block*1024))goto end;
        for(unsigned i=0;i<4;i++)if(!pop(256) || !verify(8192+block*1024+i*256))goto end;
    }
    if(!pop(0) || r.queued || r.accepted!=135168 || r.consumed!=135168 || r.dropped!=2048 || r.silence!=256)goto end;
    printf("marble-audio-queue-test: recycled PASS=1 periods=128 frames=135168 dropped=2048 silence=256 queued=0 rate=48000 bits=16 channels=2 DSP=0\n");
    passed=1;
    usleep(6000000);
end:
    printf("marble-audio-queue-test: complete PASS=%u errno=%d hardware_audio=0\n",passed,errno);
    if(audio>=0)close(audio);
    reboot(0x08);
    for(;;)usleep(200000);
}
