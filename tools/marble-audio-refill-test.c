/* Finite muted live refill test, never a HAL or audible enable command. */
#include "mini.h"
#include "marble-audio-signal.h"
#include "../xnu/files/bsd/sys/marble_audio_queue.h"
#include "../xnu/files/bsd/sys/marble_audio_refill.h"
static int16_t samples[2048];
static MarbleAudioQueueTransfer transfer;
static MarbleAudioRefillStatus status;
static int snapshot(int fd)
{
    memset(&status,0,sizeof(status));status.magic=MAREFILL_MAGIC;status.version=1;status.bytes=64;
    return !mini_syscall(SYS_ioctl,fd,MAREFILL_IOCTL,(long)&status,0,0,0) && status.magic==MAREFILL_MAGIC &&
        status.version==1 && status.bytes==64 && status.epoch && status.state!=MAREFILL_FAILED;
}
static int feed(int fd,unsigned first,unsigned count)
{
    for(unsigned i=0;i<count;i++)for(unsigned c=0;c<2;c++)samples[2*i+c]=marble_audio_signal_sample(first+i,c);
    return write(fd,samples,count*4)==count*4;
}
int main(int argc,char **argv,char **envp)
{
    (void)argc;(void)argv;(void)envp;unsigned passed=0;int audio=-1;
    int console=open("/dev/console",O_RDWR,0);
    if(console>=0){dup2(console,0);dup2(console,1);dup2(console,2);if(console>2)close(console);}
    audio=open("/dev/redwood-audio",O_WRONLY,0);if(audio<0 || !feed(audio,0,512))goto end;
    unsigned waiting=0;
    for(unsigned i=0;i<2000;i++) {
        if(!snapshot(audio))goto end;
        if(status.state==MAREFILL_WAITING){waiting=1;break;}
        usleep(1000);
    }
    if(!waiting || status.feed!=512 || status.published!=3 || status.submitted!=3 || status.completed!=3 ||
       status.pending || status.queued || status.consumed!=512 || status.silence!=256)goto end;
    printf("marble-audio-refill-test: phase=observed-empty submitted=3 completed=3 feed=512 consumed=512 silence=256 queued=0 pending=0 audible=0\n");
    if(!feed(audio,512,1024) || !feed(audio,1536,256))goto end;
    unsigned retired=0;
    for(unsigned i=0;i<2000;i++) {
        if(!snapshot(audio))goto end;
        if(status.state==MAREFILL_RETIRED){retired=1;break;}
        usleep(1000);
    }
    if(!retired || status.feed!=1792 || status.published!=8 || status.submitted!=8 || status.completed!=8 ||
       status.pending || status.queued || status.consumed!=1792 || status.silence!=256)goto end;
    for(unsigned block=0;block<7;block++) {
        memset(&transfer,0,sizeof(transfer));transfer.magic=MAQ_MAGIC;transfer.version=1;transfer.requested=256;
        if(mini_syscall(SYS_ioctl,audio,MAQ_IOCTL,(long)&transfer,0,0,0) || transfer.actual!=256 || transfer.failed)goto end;
        for(unsigned i=0;i<256;i++)for(unsigned c=0;c<2;c++)if(transfer.samples[2*i+c]!=marble_audio_signal_sample(block*256+i,c))goto end;
    }
    if(transfer.accepted!=1792 || transfer.consumed!=1792 || transfer.dropped || transfer.silence || transfer.queued)goto end;
    passed=1;
    printf("marble-audio-refill-test: phase=retired PASS=1 input_frames=1792 output_frames=2048 periods=8 consumed=1792 silence=256 queued=0 RAM_copy=1 HAL_sink=0 audible=0\n");
    usleep(6000000);
end:
    printf("marble-audio-refill-test: complete PASS=%u errno=%d audible=0\n",passed,errno);
    if(audio>=0)close(audio);reboot(0x08);for(;;)usleep(200000);
}
