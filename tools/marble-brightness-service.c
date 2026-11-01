/* Root launchd helper. The public file is on the session's RAM /private/var;
 * only the 16-byte validated request is forwarded to the raw support RAM disk.
 * No chmod of raw devices, no Android mounts and no panel access here. */
#include "mini.h"
#include "../xnuloader/marble_brightness_mailbox.h"
#define SERVICE_PATH "/private/var/run/marble-brightness"
int main(int argc,char **argv,char **envp)
{
    (void)argc;(void)argv;(void)envp;
    int raw=open("/dev/rmd1",O_RDWR,0),public=-1;
    MarbleBrightnessMailbox b;uint64_t forwarded=0;unsigned failed=0;
    if(raw<0 || pread(raw,&b,sizeof(b),MB_OFFSET)!=sizeof(b) || b.magic!=MB_MAGIC ||
       b.version!=1 || b.ready!=1 || b.failed || b.maximum!=b.original ||
       !marble_brightness_target(b.original,b.minimum,1000))goto end;
    public=open(SERVICE_PATH,O_RDWR|O_CREAT|O_EXCL,0600);
    if(public<0 || pwrite(public,&b,sizeof(b),0)!=sizeof(b) || chmod(SERVICE_PATH,0666))goto end;
    forwarded=b.sequence;
    printf("marble-brightness-service: ready=1 original=%u request_only=1 RAM_only=1\n",b.original);
    for(;;) {
        if(pread(raw,&b,sizeof(b),MB_OFFSET)!=sizeof(b) || b.magic!=MB_MAGIC || b.ready!=1 || b.failed) {failed=1;break;}
        /* Publish actual firmware capability and acknowledgement only. Never
         * replace the request line concurrently written by SpringBoard. */
        if(pwrite(public,&b,64,0)!=64 || pwrite(public,(char *)&b+128,64,128)!=64) {failed=1;break;}
        struct {uint64_t sequence;uint32_t milli,reserved;} q;
        uint64_t after;
        if(pread(public,&q,sizeof(q),64)!=sizeof(q) || pread(public,&after,8,64)!=8) {failed=1;break;}
        if(marble_brightness_request_valid(q.sequence,after,forwarded,q.milli,q.reserved)) {
            uint64_t odd=q.sequence-1;
            if(pwrite(raw,&odd,8,MB_OFFSET+64)!=8 || pwrite(raw,(char *)&q+8,8,MB_OFFSET+72)!=8 ||
               pwrite(raw,&q.sequence,8,MB_OFFSET+64)!=8) {failed=1;break;}
            forwarded=q.sequence;
            printf("marble-brightness-service: forwarded=%llu milli=%u\n",forwarded,q.milli);
        }
        if(usleep(50000)) {failed=1;break;}
    }
end:
    if(public>=0) {b.ready=0;b.failed=1;pwrite(public,&b,64,0);pwrite(public,(char *)&b+128,64,128);close(public);}
    if(raw>=0)close(raw);
    printf("marble-brightness-service: stopped failed=%u errno=%d\n",failed,errno);
    return 1;
}
