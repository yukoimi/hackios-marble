/* Reuse the project's shader/triangle path, then qualify its hardware
 * renderer and every unambiguous pixel against the triangle's geometry. */
#define main project_gl_test
#include "../userland/src/gl_test.c"
#undef main
static unsigned char pixels[128*128*4];
int main(void)
{
    io_service_t service=0;
    for(unsigned i=0;i<600 && !service;i++) {
        service=IOServiceGetMatchingService(0,IOServiceMatching("RedwoodGPU"));
        if(!service)usleep(100000);
    }
    if(!service){printf("marble-gl-test: FAIL service timeout\n");return 1;}
    IOObjectRelease(service);
    char *args[]={"gl_test","0","128","128",NULL};
    int rc=project_gl_test(4,args);
    const char *renderer=(const char *)glGetString(GL_RENDERER);
    int hardware=renderer && (strstr(renderer,"FD725") || strstr(renderer,"Adreno (TM) 725"));
    if(rc || !hardware){printf("marble-gl-test: FAIL hardware=%u rc=%u renderer=%s\n",hardware,rc,renderer?renderer:"NULL");return 1;}
    memset(pixels,0x55,sizeof(pixels));glReadPixels(0,0,128,128,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
    unsigned checked=0,bad=0,inside=0,outside=0;
    for(unsigned y=0;y<128;y++)for(unsigned x=0;x<128;x++) {
        double nx=((double)x+0.5)/64-1,ny=((double)y+0.5)/64-1;
        double blue=(ny+0.6)/1.3,green=((1-blue)+nx/0.8)/2,red=1-blue-green;
        double edge=red<green?red:green;edge=edge<blue?edge:blue;
        if(edge>-0.015 && edge<0.015)continue; /* rasterization boundary, no interpolation claim */
        int want[4]={26,26,51,255};
        if(edge>0){want[0]=(int)(red*255+0.5);want[1]=(int)(green*255+0.5);want[2]=(int)(blue*255+0.5);inside++;}else outside++;
        const unsigned char *p=pixels+4*(128*y+x);checked++;
        for(unsigned c=0;c<4;c++)if(abs((int)p[c]-want[c])>3){bad++;break;}
    }
    unsigned error=glGetError();
    printf("marble-gl-test: PASS=%u hardware=%u checked=%u inside=%u outside=%u bad=%u error=%#x renderer=%s\n",!bad && !error && inside>3000 && outside>10000,hardware,checked,inside,outside,bad,error,renderer);
    return bad || error || inside<=3000 || outside<=10000;
}
