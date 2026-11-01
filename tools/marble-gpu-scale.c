/* RAM-only, sequence-bound final GLES scale. Separate process because the
 * project's DRM shim owns one global connection per process. */
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <IOSurface/IOSurfaceRef.h>
#include <OpenGLES/ES3/gl.h>
#include <mach/mach_time.h>
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <notify.h>
#include "../userland/src/redwood_gl.h"
#include "../xnu/files/bsd/sys/marble_gpu_scale.h"
#include "marble-scale-blit.h"
#include "tests/marble-scale-pixels.h"

static struct redwood_gl *context;
static GLuint read_fbo,draw_fbo;
struct imported {IOSurfaceRef surface;GLuint texture;uint32_t sid;};
static struct imported sources[6];
static unsigned next_source;
static int audit;
static void audit_frame(const struct marble_scale_wire *job,IOSurfaceRef output)
{
    IOSurfaceRef source=NULL;
    for(unsigned i=0;i<6;i++)if(sources[i].sid==job->source)source=sources[i].surface;
    if(!source)return;
    uint32_t seed=IOSurfaceGetSeed(source);unsigned bad=0,samples=0,nonzero_source=0,nonzero_output=0;
    IOReturn a=IOSurfaceLock(source,kIOSurfaceLockReadOnly,NULL);
    if(a)return;
    IOReturn b=IOSurfaceLock(output,kIOSurfaceLockReadOnly,NULL);
    if(!b) {
        const unsigned char *src=IOSurfaceGetBaseAddress(source),*dst=IOSurfaceGetBaseAddress(output);
        size_t ss=IOSurfaceGetBytesPerRow(source),ds=IOSurfaceGetBytesPerRow(output);
        for(int gy=1;gy<=3;gy++)for(int gx=1;gx<=3;gx++) {
            int x=(int)job->width*gx/4,y=(int)job->height*gy/4;
            double sx=((x+.5)*job->source_width/job->width)-.5,sy=((y+.5)*job->source_height/job->height)-.5;
            int x0=(int)sx,y0=(int)sy,x1=x0+1,y1=y0+1;
            double fx=sx-x0,fy=sy-y0;
            if(x0<0 || y0<0 || x1>=(int)job->source_width || y1>=(int)job->source_height)continue;
            for(unsigned c=0;c<4;c++) {
                double top=src[y0*ss+x0*4+c]*(1-fx)+src[y0*ss+x1*4+c]*fx;
                double bottom=src[y1*ss+x0*4+c]*(1-fx)+src[y1*ss+x1*4+c]*fx;
                int want=(int)(top*(1-fy)+bottom*fy+.5),got=dst[y*ds+x*4+c];
                bad+=abs(got-want)>2;nonzero_source+=want!=0;nonzero_output+=got!=0;samples++;
            }
        }
        IOSurfaceUnlock(output,kIOSurfaceLockReadOnly,NULL);
    }
    IOSurfaceUnlock(source,kIOSurfaceLockReadOnly,NULL);
    printf("marble-gpu-scale-audit: seq=%llu source=%u samples=%u different=%u src_nonzero=%u out_nonzero=%u source_seed=%u,%u output_lock=%#x non_atomic_pair=1\n",(unsigned long long)job->sequence,job->source,samples,bad,nonzero_source,nonzero_output,seed,IOSurfaceGetSeed(source),b);
}

static uint64_t now_us(void)
{
    static mach_timebase_info_data_t timebase;
    if(!timebase.denom)mach_timebase_info(&timebase);
    return mach_absolute_time()*timebase.numer/timebase.denom/1000;
}
static IOSurfaceRef surface_new(int width,int height)
{
    CFMutableDictionaryRef d=CFDictionaryCreateMutable(NULL,0,&kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);
    const CFStringRef keys[]={kIOSurfaceWidth,kIOSurfaceHeight,kIOSurfaceBytesPerRow,kIOSurfaceBytesPerElement,kIOSurfacePixelFormat};
    int values[]={width,height,(width*4+63)&~63,4,0x42475241};
    for(unsigned i=0;i<5;i++) {
        CFNumberRef n=CFNumberCreate(NULL,kCFNumberIntType,&values[i]);
        CFDictionarySetValue(d,keys[i],n);CFRelease(n);
    }
    IOSurfaceRef s=IOSurfaceCreate(d);CFRelease(d);return s;
}
static GLuint surface_texture(IOSurfaceRef s)
{
    GLuint t=0;glGenTextures(1,&t);glBindTexture(GL_TEXTURE_2D,t);
    if(!redwood_gl_surface_texture(context,IOSurfaceGetID(s))) {glDeleteTextures(1,&t);return 0;}
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    if(glGetError()){glDeleteTextures(1,&t);return 0;}
    return t;
}
static void imported_drop(struct imported *s)
{
    if(s->texture)glDeleteTextures(1,&s->texture);
    if(s->surface)CFRelease(s->surface);
    memset(s,0,sizeof(*s));
}
static GLuint source_texture(const struct marble_scale_wire *job)
{
    for(unsigned i=0;i<6;i++)if(sources[i].sid==job->source) {
        /* Another DRM client has produced a new frame in this allocation.
         * Rebind the external image before attaching the cached GL name;
         * a retained IOSurface ID alone does not refresh Mesa resource state. */
        glBindTexture(GL_TEXTURE_2D,sources[i].texture);
        if(!redwood_gl_surface_texture(context,job->source) || glGetError())return 0;
        return sources[i].texture;
    }
    struct imported *s=&sources[next_source++%6];imported_drop(s);
    s->surface=IOSurfaceLookup(job->source);
    if(!s->surface)return 0;
    if(IOSurfaceGetWidth(s->surface)!=job->source_width || IOSurfaceGetHeight(s->surface)!=job->source_height ||
       IOSurfaceGetPixelFormat(s->surface)!=0x42475241) {imported_drop(s);return 0;}
    s->texture=surface_texture(s->surface);
    if(!s->texture){imported_drop(s);return 0;}
    s->sid=job->source;return s->texture;
}
static unsigned blit_surface(GLuint source,GLuint target,int sw,int sh,int dw,int dh,IOSurfaceRef output)
{
    unsigned error=marble_scale_blit(read_fbo,draw_fbo,source,target,sw,sh,dw,dh);
    /* Match EAGL's shared-image publication contract. CPU readers populate
     * cache lines even without writing; the changed seed makes the next
     * submission invalidate those lines before the GPU reuses this output.
     * glFinish alone does not publish an IOSurface seed. */
    if(!error) {
        IOReturn locked=IOSurfaceLock(output,0,NULL);
        if(locked)error=locked;
        else error=IOSurfaceUnlock(output,0,NULL);
    }
    return error;
}
static int selftest(void)
{
    const int sizes[][4]={{17,9,31,15},{63,31,19,7},{1,1,7,3},{828,1792,1080,2400}};
    unsigned cases=0,bad=0;
    for(unsigned test=0;test<sizeof(sizes)/sizeof(sizes[0]);test++) {
        int sw=sizes[test][0],sh=sizes[test][1],dw=sizes[test][2],dh=sizes[test][3];
        IOSurfaceRef src=surface_new(sw,sh),out=surface_new(dw,dh);
        GLuint a=0,b=0;
        if(!src || !out)bad++;
        else {
            b=surface_texture(out);if(!b)bad++;
            for(int frame=0;frame<(test==3?1:3) && b;frame++) {
                if(IOSurfaceLock(src,0,NULL)!=0){bad++;break;}
                scale_pattern(IOSurfaceGetBaseAddress(src),sw,sh,(int)IOSurfaceGetBytesPerRow(src),1,frame);
                IOSurfaceUnlock(src,0,NULL);
                if(!a)a=surface_texture(src);
                else {
                    glBindTexture(GL_TEXTURE_2D,a);
                    if(!redwood_gl_surface_texture(context,IOSurfaceGetID(src)) || glGetError()){bad++;break;}
                }
                unsigned error=blit_surface(a,b,sw,sh,dw,dh,out);
                if(error || IOSurfaceLock(out,kIOSurfaceLockReadOnly,NULL)!=0) {bad++;break;}
                unsigned wrong=scale_verify(IOSurfaceGetBaseAddress(out),sw,sh,dw,dh,(int)IOSurfaceGetBytesPerRow(out),1,frame);
                IOSurfaceUnlock(out,kIOSurfaceLockReadOnly,NULL);bad+=wrong;cases++;
                printf("marble-gpu-scale: pixel_case=%u frame=%d wrong=%u GL_error=%u\n",test,frame,wrong,error);
            }
        }
        if(a)glDeleteTextures(1,&a);if(b)glDeleteTextures(1,&b);
        if(src)CFRelease(src);if(out)CFRelease(out);
    }
    printf("marble-gpu-scale: selftest=%s cases=%u bad_channels=%u BGRA=1 pitch64=1 cached_name_refresh=1 full_panel=1\n",cases==10 && !bad?"PASS":"FAIL",cases,bad);
    return cases==10 && !bad;
}
static IOReturn call(io_connect_t connection,struct marble_scale_wire *job,unsigned op)
{
    job->version=MARBLE_SCALE_VERSION;job->op=op;size_t size=sizeof(*job);
    struct marble_scale_wire result={0};
    IOReturn r=IOConnectCallStructMethod(connection,MARBLE_SCALE_SELECTOR,job,sizeof(*job),&result,&size);
    if(r==0 && size!=sizeof(*job))return kIOReturnBadArgument;
    if(!r)*job=result;
    return r;
}
int main(void)
{
    setvbuf(stdout,NULL,_IOLBF,0);
    if(getenv("MARBLE_SCALE_WAIT_COMPOSITOR")) {
        int token=0;uint64_t state=0;unsigned polls=0;
        uint32_t rc=notify_register_check("com.marble.gpu.compositor-ready",&token);
        if(rc!=NOTIFY_STATUS_OK)return 2;
        for(;polls<300;polls++) {
            rc=notify_get_state(token,&state);
            if(rc!=NOTIFY_STATUS_OK || state==1)break;
            usleep(100000);
        }
        notify_cancel(token);
        printf("marble-gpu-scale: compositor_first_frame_gate=%u status=%u polls=%u GPU_not_opened_yet=1\n",state==1 && rc==NOTIFY_STATUS_OK,rc,polls);
        if(state!=1 || rc!=NOTIFY_STATUS_OK)return 2;
    }
    const char *delay=getenv("MARBLE_SCALE_START_DELAY");
    if(delay) {
        if(strcmp(delay,"15") && strcmp(delay,"30"))return 2;
        unsigned seconds=(unsigned)strtoul(delay,NULL,10);
        printf("marble-gpu-scale: deferred_GPU_open_seconds=%u initialization_order_test=1\n",seconds);
        sleep(seconds);
    }
    audit=getenv("MARBLE_SCALE_AUDIT") && !strcmp(getenv("MARBLE_SCALE_AUDIT"),"1");
    printf("marble-gpu-scale: Mesa_debug=%s\n",getenv("FD_MESA_DEBUG")?getenv("FD_MESA_DEBUG"):"default");
    int (*set_qos)(unsigned,int)=dlsym(RTLD_DEFAULT,"pthread_set_qos_class_self_np");
    printf("marble-gpu-scale: interactive_qos=%d\n",set_qos?set_qos(0x21,0):-1);
    io_service_t service=0;io_connect_t connection=0;
    IOSurfaceRef output=NULL;GLuint target=0;int rc=1;
    for(unsigned i=0;i<300 && !service;i++) {
        service=IOServiceGetMatchingService(0,IOServiceMatching("RedwoodGPU"));
        if(!service)usleep(100000);
    }
    if(!service){printf("marble-gpu-scale: no GPU service\n");goto done;}
    IOReturn ret=IOServiceOpen(service,mach_task_self(),0,&connection);IOObjectRelease(service);
    if(ret){printf("marble-gpu-scale: open=%#x\n",ret);goto done;}
    struct marble_scale_wire job={0};
    for(unsigned i=0;i<300;i++) {
        ret=call(connection,&job,MarbleScaleQuery);
        if(ret!=kIOReturnNotReady)break;
        usleep(100000);
    }
    if(ret || !marble_scale_layout(job.width,job.height,job.stride,(uint64_t)job.stride*job.height)) {
        printf("marble-gpu-scale: query=%#x invalid layout\n",ret);goto done;
    }
    context=redwood_gl_create(3);
    if(!context || !redwood_gl_make_current(context)){printf("marble-gpu-scale: no ES3 context\n");goto done;}
    const char *renderer=(const char *)glGetString(GL_RENDERER);
    int hardware=renderer && (strstr(renderer,"FD725") || strstr(renderer,"Adreno (TM) 725"));
    printf("marble-gpu-scale: renderer=%s hardware=%d\n",renderer?renderer:"NULL",hardware);
    if(!hardware)goto done;
    glGenFramebuffers(1,&read_fbo);glGenFramebuffers(1,&draw_fbo);
    /* Fail closed before registering: verify actual IOSurface imports,
     * colour order, orientation, padded pitch and changing frames on FD725. */
    if(!selftest())goto done;
    output=surface_new((int)job.width,(int)job.height);
    if(!output || IOSurfaceGetBytesPerRow(output)!=job.stride)goto done;
    target=surface_texture(output);if(!target)goto done;
    job.output=IOSurfaceGetID(output);ret=call(connection,&job,MarbleScaleStart);
    if(ret){printf("marble-gpu-scale: start=%#x\n",ret);goto done;}
    printf("marble-gpu-scale: ready=1 geometry=%ux%u pitch=%u output=%u\n",job.width,job.height,job.stride,job.output);
    unsigned frames=0;uint64_t elapsed=0;
    for(;;) {
        ret=call(connection,&job,MarbleScaleWait);
        if(ret==kIOReturnTimeout)continue;
        if(ret){printf("marble-gpu-scale: wait=%#x frames=%u\n",ret,frames);break;}
        uint64_t began=now_us();GLuint source=source_texture(&job);
        job.status=blit_surface(source,target,job.source_width,job.source_height,job.width,job.height,output);
        if(audit && !job.status && (frames<12 || !(frames%120)))audit_frame(&job,output);
        uint64_t cost=now_us()-began;elapsed+=cost;frames++;
        ret=call(connection,&job,MarbleScaleComplete);
        if(frames<=8 || !(frames%120) || ret || job.status)
            printf("marble-gpu-scale: seq=%llu frames=%u scale_us=%llu mean_us=%llu GL_error=%u complete=%#x\n",
                (unsigned long long)job.sequence,frames,(unsigned long long)cost,(unsigned long long)(elapsed/frames),job.status,ret);
        if(ret || job.status)break;
    }
    rc=0;
done:
    /* Close the registration before releasing memory. Kernel/GPU references
     * retain any outstanding imports through their completion fence. */
    if(connection)IOServiceClose(connection);
    if(context) {
        for(unsigned i=0;i<6;i++)imported_drop(&sources[i]);
        if(target)glDeleteTextures(1,&target);
        if(read_fbo)glDeleteFramebuffers(1,&read_fbo);
        if(draw_fbo)glDeleteFramebuffers(1,&draw_fbo);
        redwood_gl_make_current(NULL);redwood_gl_destroy(context);
    }
    if(output)CFRelease(output);
    printf("marble-gpu-scale: exit=%d\n",rc);return rc;
}
