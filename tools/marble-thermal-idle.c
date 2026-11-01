/* Static /sbin/launchd for a tiny RAM APFS. No iOS, GPU, devices or stress.
 * Reuses userland/libmini's sleeping syscall wrapper. The loader budget is
 * 18 seconds; this process also requests a normal halt after 20 seconds. */
#include "mini.h"

int main(int argc, char **argv, char **envp)
{
    (void)argc; (void)argv; (void)envp;
    int fd=open("/dev/console",O_RDWR,0);
    if(fd>=0) {
        dup2(fd,0);dup2(fd,1);dup2(fd,2);
        if(fd>2)close(fd);
    }
    printf("marble-thermal-idle: sleeping 20 seconds, GPU disabled\n");
    for(unsigned i=0;i<100;i++) {
        if(usleep(200000)!=0) {
            printf("marble-thermal-idle: sleep failed errno=%d; halt\n",errno);
            reboot(0x08);
            return 1;
        }
    }
    printf("marble-thermal-idle: normal halt\n");
    reboot(0x08);
    for(;;)usleep(200000);
}
