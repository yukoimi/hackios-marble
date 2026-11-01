/* Observe stock iOS brightness requests. MARBLE_BRIGHTNESS_BACKEND additionally
 * advertises an already-qualified RAM/DCS backend and forwards SB requests to
 * it. The default build remains diagnostic only. No preference/ALS writes.
 * BKS ABI reference: out/daily-brightness-api-v1-plan/references.json. */
#include <stddef.h>
#include <stdint.h>
extern long write(int,const void *,size_t);
extern int snprintf(char *,size_t,const char *,...);
#ifndef MARBLE_BRIGHTNESS_INVENTORY
#define MARBLE_BRIGHTNESS_INVENTORY 1
#endif
#ifdef MARBLE_BRIGHTNESS_BACKEND
#include "../xnuloader/marble_brightness_mailbox.h"
extern int open(const char *,int,...);
extern long pread(int,void *,size_t,long long);
extern long pwrite(int,const void *,size_t,long long);
extern int *__error(void);
extern unsigned getuid(void);
static int brightness_fd=-1;
static uint64_t brightness_sequence;
static unsigned brightness_queued=1001;
static unsigned brightness_writing;
static int brightness_backend(void)
{
    static unsigned open_errors;
    if(brightness_fd<0)brightness_fd=open("/private/var/run/marble-brightness",2,0);
    if(brightness_fd<0) {
        if(open_errors++<4) {
            char line[160];int n=snprintf(line,sizeof(line),"marble-brightness-backend: RAM_service_open_failed errno=%d uid=%u\n",*__error(),getuid());
            if(n>0 && n<(int)sizeof(line))write(2,line,(size_t)n);
        }
        return 0;
    }
    MarbleBrightnessMailbox b;
    if(pread(brightness_fd,&b,sizeof(b),0)!=sizeof(b))return 0;
    return b.magic==MB_MAGIC && b.version==1 && b.ready==1 && !b.failed &&
        b.maximum==b.original && b.original<=4095 &&
        b.minimum==(b.original>327?328u:8u) && b.minimum<b.original;
}
static void brightness_submit(float level)
{
    if(!(level>=0 && level<=1) || !brightness_backend())return;
    unsigned milli=(unsigned)(level*1000+.5f);
    /* SB normally applies on its main queue. Decline a concurrent/reentrant
     * writer rather than publishing overlapping seqlocks. */
    if(__atomic_exchange_n(&brightness_writing,1,__ATOMIC_ACQUIRE))return;
    MarbleBrightnessMailbox b;
    if(pread(brightness_fd,&b,sizeof(b),0)!=sizeof(b))goto done;
    if(b.acknowledged>brightness_sequence)brightness_sequence=b.acknowledged;
    if(b.sequence==UINT64_MAX)goto done;
    if(b.sequence>brightness_sequence)brightness_sequence=b.sequence+(b.sequence&1);
    if(milli==brightness_queued || brightness_sequence>UINT64_MAX-2)goto done;
    uint64_t odd=brightness_sequence+1,even=brightness_sequence+2;
    struct {uint32_t milli,reserved;} request={milli,0};
    if(pwrite(brightness_fd,&odd,8,64)!=8 ||
       pwrite(brightness_fd,&request,8,72)!=8 ||
       pwrite(brightness_fd,&even,8,64)!=8)goto done;
    brightness_sequence=even;brightness_queued=milli;
    char line[192];int n=snprintf(line,sizeof(line),"marble-brightness-submit: seq=%llu milli=%u RAM_only=1\n",even,milli);
    if(n>0 && n<(int)sizeof(line))write(2,line,(size_t)n);
done:
    __atomic_store_n(&brightness_writing,0,__ATOMIC_RELEASE);
}
#else
static int brightness_backend(void) {return 0;}
#endif
extern void *dlsym(void *,const char *);
extern long write(int,const void *,size_t);
extern int getpid(void);
extern void *malloc(size_t);
extern void free(void *);
extern char *strstr(const char *,const char *);
extern int strcmp(const char *,const char *);
extern void *objc_getClass(const char *);
extern void *object_getClass(void *);
extern void objc_msgSend(void);
extern void *sel_registerName(const char *);
extern void *class_getInstanceMethod(void *,void *);
extern void *method_setImplementation(void *,void *);
extern int objc_getClassList(void **,int);
extern const char *class_getName(void *);
extern void **class_copyMethodList(void *,unsigned *);
extern void *method_getName(void *);
extern const char *sel_getName(void *);
extern const char *method_getTypeEncoding(void *);
extern void *method_getImplementation(void *);
extern int snprintf(char *,size_t,const char *,...);
extern void *dispatch_get_global_queue(long,unsigned long);
extern void *dispatch_source_create(const void *,uintptr_t,unsigned long,void *);
extern void dispatch_source_set_timer(void *,uint64_t,uint64_t,uint64_t);
extern void dispatch_source_set_event_handler_f(void *,void (*)(void *));
extern void dispatch_source_cancel(void *);
extern void dispatch_resume(void *);
extern uint64_t dispatch_time(uint64_t,int64_t);
extern const char _dispatch_source_type_timer[];
static void *timer;
static float (*current)(void);
static unsigned samples;
static int previous=-2;
static void (*original_set)(void *,void *,float,void *,unsigned long long);
static _Bool (*original_sb_set)(void *,void *,float,_Bool);
static void (*original_sb_apply)(void *,void *,float,_Bool);
static void (*original_ui_send)(void *,void *,void *);
struct marble_point {double x,y;};
struct marble_rect {double x,y,w,h;};
static unsigned ui_geometry;
static _Bool (*original_available)(void *,void *);
static _Bool (*original_ca_available)(void *,void *);
static unsigned availability_logs;
static void available_log(const char *kind,_Bool available)
{
    if(availability_logs++>=24)return;
    char line[192];int n=snprintf(line,sizeof(line),"marble-brightness-available: source=%s stock=%u backend=%u\n",kind,available,brightness_backend());
    if(n>0 && n<(int)sizeof(line))write(2,line,(size_t)n);
}
static _Bool observe_available(void *self,void *sel)
{_Bool r=original_available(self,sel);available_log("BK",r);return r || brightness_backend();}
static _Bool observe_ca_available(void *self,void *sel)
{_Bool r=original_ca_available(self,sel);available_log("CA",r);return r;}
static void install_available(void)
{
    const char *names[]={"BKDisplayBrightnessController","CBBrightnessProxyCA"};
    const char *sels[]={"isBrightnessLevelControlAvailable","isBrightnessAvailable"};
    for(unsigned i=0;i<2;i++) {
        void *c=objc_getClass(names[i]),*m=c?class_getInstanceMethod(c,sel_registerName(sels[i])):NULL;
        const char *types=m?method_getTypeEncoding(m):NULL;
        if(!types || strcmp(types,"B16@0:8"))continue;
        if(i==0 && !original_available) {
            original_available=(_Bool (*)(void *,void *))method_getImplementation(m);
            if(original_available)method_setImplementation(m,(void *)observe_available);
        } else if(i==1 && !original_ca_available) {
            original_ca_available=(_Bool (*)(void *,void *))method_getImplementation(m);
            if(original_ca_available)method_setImplementation(m,(void *)observe_ca_available);
        }
    }
}
static void ui_view_log(void *view,unsigned depth,unsigned *count)
{
    if(!view || depth>3 || (*count)++>=24)return;
    struct marble_rect rect=((struct marble_rect (*)(void *,void *))objc_msgSend)(view,sel_registerName("frame"));
    _Bool enabled=((_Bool (*)(void *,void *))objc_msgSend)(view,sel_registerName("isUserInteractionEnabled"));
    _Bool hidden=((_Bool (*)(void *,void *))objc_msgSend)(view,sel_registerName("isHidden"));
    char line[384];const char *name=class_getName(object_getClass(view));
    int n=snprintf(line,sizeof(line),"marble-ui-view: depth=%u class=%s frame=%d,%d,%d,%d interaction=%u hidden=%u\n",depth,name?name:"?",(int)rect.x,(int)rect.y,(int)rect.w,(int)rect.h,enabled,hidden);
    if(n>0 && n<(int)sizeof(line))write(2,line,(size_t)n);
    void *list=((void *(*)(void *,void *))objc_msgSend)(view,sel_registerName("subviews"));
    unsigned long total=((unsigned long (*)(void *,void *))objc_msgSend)(list,sel_registerName("count"));
    for(unsigned long i=0;i<total && *count<24;i++)
        ui_view_log(((void *(*)(void *,void *,unsigned long))objc_msgSend)(list,sel_registerName("objectAtIndex:"),i),depth+1,count);
}
static void observe_ui_send(void *self,void *selector,void *event)
{
    original_ui_send(self,selector,event);
    /* Public UIKit selectors, exact method encodings checked at installation.
     * Observe the actual hit-test destination; never reroute input. */
    void *set=((void *(*)(void *,void *))objc_msgSend)(event,sel_registerName("allTouches"));
    if(!set)return;
    void *touch=((void *(*)(void *,void *))objc_msgSend)(set,sel_registerName("anyObject"));
    if(!touch)return;
    long phase=((long (*)(void *,void *))objc_msgSend)(touch,sel_registerName("phase"));
    if(phase!=0 && phase!=3 && phase!=4)return;
    void *view=((void *(*)(void *,void *))objc_msgSend)(touch,sel_registerName("view"));
    if(!view)return;
    char line[256];const char *name=class_getName(object_getClass(view));
    int n=snprintf(line,sizeof(line),"marble-ui-hit: phase=%ld view=%s forwarded=1\n",phase,name?name:"?");
    if(n>0 && n<(int)sizeof(line))write(2,line,(size_t)n);
    if(ui_geometry && phase==0) {
        struct marble_point point=((struct marble_point (*)(void *,void *,void *))objc_msgSend)(touch,sel_registerName("locationInView:"),NULL);
        n=snprintf(line,sizeof(line),"marble-ui-point: window_x=%d window_y=%d\n",(int)point.x,(int)point.y);
        if(n>0 && n<(int)sizeof(line))write(2,line,(size_t)n);
        unsigned count=0;ui_view_log(view,0,&count);
    }
}
static int exact_method(const char *cls,const char *sel,const char *types)
{
    void *c=objc_getClass(cls),*m=c?class_getInstanceMethod(c,sel_registerName(sel)):NULL;
    const char *t=m?method_getTypeEncoding(m):NULL;return t && !strcmp(t,types);
}
static void install_ui_observer(void)
{
    if(original_ui_send || !exact_method("UIApplication","sendEvent:","v24@0:8@16") ||
       !exact_method("UIEvent","allTouches","@16@0:8") ||
       !exact_method("NSSet","anyObject","@16@0:8") ||
       !exact_method("UITouch","phase","q16@0:8") ||
       !exact_method("UITouch","view","@16@0:8"))return;
    void *m=class_getInstanceMethod(objc_getClass("UIApplication"),sel_registerName("sendEvent:"));
    original_ui_send=(void (*)(void *,void *,void *))method_getImplementation(m);
    if(original_ui_send)method_setImplementation(m,(void *)observe_ui_send);
    ui_geometry=exact_method("UITouch","locationInView:","{CGPoint=dd}24@0:8@16") &&
        exact_method("UIView","frame","{CGRect={CGPoint=dd}{CGSize=dd}}16@0:8") &&
        exact_method("UIView","isUserInteractionEnabled","B16@0:8") &&
        exact_method("UIView","isHidden","B16@0:8") &&
        exact_method("UIView","subviews","@16@0:8") &&
        exact_method("NSArray","count","Q16@0:8") &&
        exact_method("NSArray","objectAtIndex:","@24@0:8Q16");
}
static void sb_log(const char *method,float level)
{
    int valid=level>=0 && level<=1;char line[192];
    int n=snprintf(line,sizeof(line),"marble-brightness-sb: pid=%d method=%s valid=%d milli=%d forwarded=1 panel_writes=0\n",
        getpid(),method,valid,valid?(int)(level*1000+.5f):-1);
    if(n>0 && n<(int)sizeof(line))write(2,line,(size_t)n);
}
static _Bool observe_sb_set(void *self,void *selector,float level,_Bool animated)
{
    _Bool result=original_sb_set(self,selector,level,animated);
    sb_log("set",level);return result;
}
static void observe_sb_apply(void *self,void *selector,float level,_Bool animated)
{
    original_sb_apply(self,selector,level,animated);sb_log("apply",level);
}
static void install_sb_observer(void)
{
    void *cls=objc_getClass("SBDisplayBrightnessController");
    if(!cls)return;
    void *m=class_getInstanceMethod(cls,sel_registerName("setBrightnessLevel:animated:"));
    const char *types=m?method_getTypeEncoding(m):NULL;
    if(!original_sb_set && types && !strcmp(types,"B24@0:8f16B20")) {
        original_sb_set=(_Bool (*)(void *,void *,float,_Bool))method_getImplementation(m);
        if(original_sb_set)method_setImplementation(m,(void *)observe_sb_set);
    }
    m=class_getInstanceMethod(cls,sel_registerName("_setBrightnessLevel:animated:"));
    types=m?method_getTypeEncoding(m):NULL;
    if(!original_sb_apply && types && !strcmp(types,"v24@0:8f16B20")) {
        original_sb_apply=(void (*)(void *,void *,float,_Bool))method_getImplementation(m);
        if(original_sb_apply)method_setImplementation(m,(void *)observe_sb_apply);
    }
}
/* Exact signature observed on this iOS image in inventory-v1-live logs:
 * BKDisplayBrightnessController setBrightnessLevel:reason:options:
 * v36@0:8f16@20Q28. Forward once, unchanged; no panel or preference writes. */
static void observe_set(void *self,void *selector,float level,void *reason,unsigned long long options)
{
    if(original_set)original_set(self,selector,level,reason,options);
    int valid=level>=0 && level<=1;
    char line[256];
    int n=snprintf(line,sizeof(line),"marble-brightness-request: pid=%d valid=%d milli=%d options=%llu forwarded=1 panel_writes=0\n",
        getpid(),valid,valid?(int)(level*1000+.5f):-1,options);
    if(n>0 && n<(int)sizeof(line))write(2,line,(size_t)n);
#ifdef MARBLE_BRIGHTNESS_BACKEND
    /* The real CC path reaches this BK setter, observed on device. It is the
     * sole producer; SB's diagnostic apply hook does not write requests. */
    brightness_submit(level);
#endif
}
static void install_observer(void)
{
    if(original_set)return;
    void *cls=objc_getClass("BKDisplayBrightnessController");
    void *method=cls?class_getInstanceMethod(cls,sel_registerName("setBrightnessLevel:reason:options:")):NULL;
    const char *types=method?method_getTypeEncoding(method):NULL;
    if(!types || strcmp(types,"v36@0:8f16@20Q28"))return;
    original_set=(void (*)(void *,void *,float,void *,unsigned long long))method_getImplementation(method);
    if(!original_set)return;
    method_setImplementation(method,(void *)observe_set);
    char line[160];int n=snprintf(line,sizeof(line),"marble-brightness-hook: pid=%d exact_abi=1 forwarding=1 panel_writes=0\n",getpid());
    if(n>0 && n<(int)sizeof(line))write(2,line,(size_t)n);
}
#if MARBLE_BRIGHTNESS_INVENTORY
static void inventory(void)
{
    int count=objc_getClassList(NULL,0);
    if(count<=0 || count>100000)return;
    void **classes=malloc((size_t)count*sizeof(*classes));
    if(!classes)return;
    int actual=objc_getClassList(classes,count);if(actual<count)count=actual;
    unsigned lines=0;
    for(int i=0;i<count && lines<300;i++) {
        const char *cls=class_getName(classes[i]);
        if(!cls || strstr(cls,"Keyboard") ||
           (!strstr(cls,"Brightness") && !strstr(cls,"Backlight") && !strstr(cls,"DisplayModule")))continue;
        unsigned methods=0;void **list=class_copyMethodList(classes[i],&methods);
        for(unsigned j=0;j<methods && lines<300;j++) {
            const char *name=sel_getName(method_getName(list[j]));
            if(!name || (!strstr(name,"rightness") && !strstr(name,"acklight") && !strstr(name,"lider") &&
                         !strstr(name,"alue") && !strstr(name,"Property")))continue;
            char line[512];const char *types=method_getTypeEncoding(list[j]);
            int n=snprintf(line,sizeof(line),"marble-brightness-abi: pid=%d class=%s method=%s types=%s imp=%llx panel_writes=0\n",
                getpid(),cls,name,types?types:"?",(unsigned long long)(uintptr_t)method_getImplementation(list[j]));
            if(n>0 && n<(int)sizeof(line))write(2,line,(size_t)n);
            lines++;
        }
        free(list);
    }
    free(classes);
}
#endif
static void observe(void *unused)
{
    (void)unused;
    if(samples<10) {install_observer();install_sb_observer();install_ui_observer();install_available();}
#if MARBLE_BRIGHTNESS_INVENTORY
    if(samples==0 || samples==50)inventory();
#endif
    if(!current)current=dlsym((void *)-2,"BKSDisplayBrightnessGetCurrent");
    float value=current?current():-1;
    int valid=value>=0 && value<=1;
    int milli=valid?(int)(value*1000+.5f):-1;
    if(milli!=previous || samples%10==0) {
        char line[192];
        int n=snprintf(line,sizeof(line),"marble-brightness-ui: pid=%d sample=%u api=%u valid=%u milli=%d panel_writes=0\n",
                       getpid(),samples,current!=NULL,valid,milli);
        if(n>0 && n<(int)sizeof(line))write(2,line,(size_t)n);
        previous=milli;
    }
    if(++samples>=90)dispatch_source_cancel(timer);
}
__attribute__((constructor)) static void start(void)
{
    install_observer();install_sb_observer();install_ui_observer();install_available();
    timer=dispatch_source_create(_dispatch_source_type_timer,0,0,dispatch_get_global_queue(-17,0));
    if(!timer)return;
    dispatch_source_set_timer(timer,dispatch_time(0,2000000000LL),1000000000ULL,100000000ULL);
    dispatch_source_set_event_handler_f(timer,observe);
    dispatch_resume(timer);
}
