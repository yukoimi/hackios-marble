/* RAM-only static PID1; writes a finite diagnostic signal, never enables amps. */
#include "mini.h"
#include "marble-audio-signal.h"
#include "../xnu/files/bsd/sys/marble_audio_queue.h"
static int16_t samples[2048];
static MarbleAudioQueueTransfer transfer;
int main(int argc,char **argv,char **envp)
{
    (void)argc;(void)argv;(void)envp;unsigned passed=0;
    int console=open("/dev/console",O_RDWR,0);
    if(console>=0){dup2(console,0);dup2(console,1);dup2(console,2);if(console>2)close(console);}
    int audio=open("/dev/redwood-audio",O_WRONLY,0);if(audio<0)goto end;
    for(unsigned block=0;block<2;block++) {
        for(unsigned i=0;i<1024;i++)for(unsigned c=0;c<2;c++)samples[2*i+c]=marble_audio_signal_sample(block*1024+i,c);
        if(write(audio,samples,sizeof(samples))!=sizeof(samples))goto end;
    }
    for(unsigned block=0;block<8;block++) {
        memset(&transfer,0,sizeof(transfer));transfer.magic=MAQ_MAGIC;transfer.version=1;transfer.requested=256;
        if(mini_syscall(SYS_ioctl,audio,MAQ_IOCTL,(long)&transfer,0,0,0) || transfer.actual!=256 || transfer.failed)goto end;
        for(unsigned i=0;i<256;i++)for(unsigned c=0;c<2;c++)if(transfer.samples[2*i+c]!=marble_audio_signal_sample(block*256+i,c))goto end;
    }
    if(transfer.accepted!=2048 || transfer.consumed!=2048 || transfer.dropped || transfer.silence || transfer.queued)goto end;
    passed=1;
    printf("marble-audio-signal-test: writes_and_RAM_copy PASS=1 frames=2048 rate=48000 bits=16 channels=2 pattern=triangle1k raw_peak=504 consumed=2048 dropped=0 queued=0 audible=0\n");
    usleep(6000000);
end:
    printf("marble-audio-signal-test: complete PASS=%u errno=%d audible=0\n",passed,errno);
    if(audio>=0)close(audio);reboot(0x08);for(;;)usleep(200000);
}
