/* Finite stream24 producer: input exceeds the 4096-frame queue capacity.
 * The only deliberate gap is period two. No HAL, power or unmute controls. */
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
static int feed_and_check(int fd,unsigned first,unsigned count)
{
    for(unsigned i=0;i<count;i++)for(unsigned c=0;c<2;c++)samples[2*i+c]=marble_audio_signal_sample(first+i,c);
    if(write(fd,samples,count*4)!=count*4)return 0;
    for(unsigned at=0;at<count;at+=256) {
        memset(&transfer,0,sizeof(transfer));transfer.magic=MAQ_MAGIC;transfer.version=1;transfer.requested=256;
        if(mini_syscall(SYS_ioctl,fd,MAQ_IOCTL,(long)&transfer,0,0,0) || transfer.actual!=256 || transfer.failed)return 0;
        for(unsigned i=0;i<256;i++)for(unsigned c=0;c<2;c++)if(transfer.samples[2*i+c]!=marble_audio_signal_sample(first+at+i,c))return 0;
    }
    return !transfer.dropped && !transfer.silence && !transfer.queued && transfer.accepted==first+count && transfer.consumed==first+count;
}
int main(int argc,char **argv,char **envp)
{
    (void)argc;(void)argv;(void)envp;unsigned passed=0;int audio=-1;
    int console=open("/dev/console",O_RDWR,0);
    if(console>=0){dup2(console,0);dup2(console,1);dup2(console,2);if(console>2)close(console);}
    audio=open("/dev/redwood-audio",O_WRONLY,0);if(audio<0 || !feed_and_check(audio,0,512))goto end;
    unsigned waiting=0;
    for(unsigned i=0;i<2000;i++) {
        if(!snapshot(audio))goto end;
        if(status.state==MAREFILL_WAITING){waiting=1;break;}
        usleep(1000);
    }
    if(!waiting || status.feed!=512 || status.published!=3 || status.submitted!=3 || status.completed!=3 ||
       status.pending || status.queued || status.consumed!=512 || status.silence!=256)goto end;
    printf("marble-audio-stream-test: phase=observed-empty submitted=3 completed=3 feed=512 consumed=512 silence=256 queued=0 pending=0 audible=0\n");
    if(!feed_and_check(audio,512,1024))goto end;
    unsigned first=1536;
    for(unsigned poll=0;first<5888 && poll<2000;poll++) {
        if(!snapshot(audio) || status.feed!=first || status.silence!=256 || status.queued>2048 ||
           status.consumed+status.queued!=status.feed || status.state==MAREFILL_RETIRED)goto end;
        if(status.queued<=1536) {
            if(!feed_and_check(audio,first,256))goto end;
            first+=256;
        } else usleep(1000);
    }
    if(first!=5888)goto end;
    unsigned retired=0;
    for(unsigned i=0;i<2000;i++) {
        if(!snapshot(audio))goto end;
        if(status.state==MAREFILL_RETIRED){retired=1;break;}
        usleep(1000);
    }
    if(!retired || status.feed!=5888 || status.published!=24 || status.submitted!=24 || status.completed!=24 ||
       status.pending || status.queued || status.consumed!=5888 || status.silence!=256)goto end;
    passed=1;
    printf("marble-audio-stream-test: phase=retired PASS=1 input_frames=5888 output_frames=6144 periods=24 consumed=5888 silence=256 queued=0 RAM_copy=1 flow_control=1 writes=19 HAL_sink=0 audible=0\n");
    usleep(6000000);
end:
    printf("marble-audio-stream-test: complete PASS=%u errno=%d audible=0\n",passed,errno);
    if(audio>=0)close(audio);reboot(0x08);for(;;)usleep(200000);
}
