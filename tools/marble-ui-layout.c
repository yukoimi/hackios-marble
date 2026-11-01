/* Qualified 2x display shape compatibility and opt-in runtime evidence. */
#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>
#include <dispatch/dispatch.h>
#include <objc/runtime.h>
#include <objc/message.h>
#include <mach-o/dyld.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <math.h>
#include <dlfcn.h>
#include <notify.h>
#include <fcntl.h>
#include <pthread.h>
#include <time.h>
#include <mach/mach.h>
#include <mach/mach_time.h>
#include "../userland/src/marble_control_input.h"
#include <mach/arm/thread_status.h>
extern kern_return_t mach_vm_read_overwrite(vm_map_t,mach_vm_address_t,mach_vm_size_t,mach_vm_address_t,mach_vm_size_t *);
extern const void *_dyld_get_shared_cache_range(size_t *);
#define SEND(type,target,name) ((type(*)(id,SEL))objc_msgSend)((id)(target),sel_registerName(name))
static id (*original_exclusion)(id,SEL);
static id normalized_exclusion(id self,SEL selector)
{
    id original=original_exclusion(self,selector);
    static unsigned traced;
    if(traced++<2)fprintf(stderr,"marble-ui-layout: exclusion_call receiver=%s original=%s\n",object_getClassName(self),original?object_getClassName(original):"nil");
    CGRect bounds=SEND(CGRect,self,"bounds"),native=SEND(CGRect,self,"nativeBounds");
    double scale=SEND(double,self,"scale");
    if(fabs(scale-2)>0.001 || fabs(bounds.size.width-414)>0.01 || fabs(bounds.size.height-896)>0.01 ||
       fabs(native.size.width-828)>0.01 || fabs(native.size.height-1792)>0.01)return original;
    if(!original || strcmp(object_getClassName(original),"UISDisplaySingleRectShape"))return original;
    CGRect r=SEND(CGRect,original,"rect");
    // Match the stock Split828 provider; 2x rasterization formerly retained
    // the 3x iPhone-X exclusion shape (209.333 x 30). No status bar masking.
    if(fabs(r.size.width-209.333333)>0.01 || fabs(r.size.height-30)>0.01)return original;
    static dispatch_once_t once;static id shape;
    dispatch_once(&once,^{
        Class provider=objc_getClass("_UIStatusBarVisualProvider_Split828");
        CGSize sz=SEND(CGSize,provider,"notchSize");
        if(fabs(sz.width-227)>0.01 || fabs(sz.height-32)>0.01)return;
        CGRect rect=CGRectMake((414-sz.width)/2,0,sz.width,sz.height);
        shape=((id(*)(id,SEL,CGRect))objc_msgSend)(SEND(id,object_getClass(original),"alloc"),sel_registerName("initWithRect:"),rect);
        fprintf(stderr,"marble-ui-layout: normalized_exclusion=%gx%g screen=414x896@2 stock_provider=Split828 process=%s\n",sz.width,sz.height,getprogname());
    });
    return shape?shape:original;
}
static void install_geometry(void)
{
    if(access("/redwood/etc/ui-geometry-2x",F_OK))return;
    Class screen=objc_getClass("UIScreen"),shape=objc_getClass("UISDisplaySingleRectShape");
    Method get=class_getInstanceMethod(screen,sel_registerName("_exclusionArea"));
    Method init=class_getInstanceMethod(shape,sel_registerName("initWithRect:"));
    if(!get || !init || strcmp(method_getTypeEncoding(get),"@16@0:8") ||
       strcmp(method_getTypeEncoding(init),"@48@0:8{CGRect={CGPoint=dd}{CGSize=dd}}16")) {
        fprintf(stderr,"marble-ui-layout: geometry_abi_mismatch hooks=0\n");return;
    }
    size_t size=0;uintptr_t base=(uintptr_t)_dyld_get_shared_cache_range(&size),slide=base-0x180000000ULL;
    if((uintptr_t)method_getImplementation(init)-slide!=0x193436944ULL)return;
    original_exclusion=(void *)method_setImplementation(get,(IMP)normalized_exclusion);
    fprintf(stderr,"marble-ui-layout: geometry_hook=1 process=%s\n",getprogname());
}
static void methods(const char *name,int class_methods)
{
    Class c=objc_getClass(name);if(!c)return;
    if(class_methods)c=object_getClass((id)c);
    unsigned count=0;Method *list=class_copyMethodList(c,&count);
    size_t length=0;uintptr_t base=(uintptr_t)_dyld_get_shared_cache_range(&length);
    uintptr_t slide=base?base-0x180000000ULL:0;
    for(unsigned i=0;i<count;i++) {
        Method m=list[i];
        fprintf(stderr,"marble-ui-layout: %c[%s %s] type=%s imp=%#llx\n",class_methods?'+':'-',name,
            sel_getName(method_getName(m)),method_getTypeEncoding(m),
            (unsigned long long)((uintptr_t)method_getImplementation(m)-slide));
    }
    free(list);
}
static void screen(void)
{
    Class cls=objc_getClass("UIScreen");if(!cls)return;
    id s=SEND(id,cls,"mainScreen");if(!s)return;
    fprintf(stderr,"marble-ui-layout: main_screen_class=%s\n",object_getClassName(s));
    const char *rects[]={"bounds","nativeBounds"};
    for(unsigned i=0;i<2;i++)if(class_getInstanceMethod(object_getClass(s),sel_registerName(rects[i]))) {
        CGRect r=SEND(CGRect,s,rects[i]);
        fprintf(stderr,"marble-ui-layout: screen=%s x=%g y=%g width=%g height=%g\n",rects[i],r.origin.x,r.origin.y,r.size.width,r.size.height);
    }
    const char *numbers[]={"scale","nativeScale","_displayCornerRadius","nativeDisplayWidth"};
    for(unsigned i=0;i<sizeof(numbers)/sizeof(numbers[0]);i++)if(class_getInstanceMethod(object_getClass(s),sel_registerName(numbers[i])))
        fprintf(stderr,"marble-ui-layout: screen=%s value=%g\n",numbers[i],SEND(double,s,numbers[i]));
    id exclusion=SEND(id,s,"_exclusionArea");
    if(exclusion) {
        CGRect r=SEND(CGRect,exclusion,"rect");
        fprintf(stderr,"marble-ui-layout: exclusion class=%s x=%g y=%g width=%g height=%g\n",object_getClassName(exclusion),r.origin.x,r.origin.y,r.size.width,r.size.height);
        methods(object_getClassName(exclusion),0);
    }
    unsigned classes_count=0;Class *all=objc_copyClassList(&classes_count);
    for(unsigned i=0;i<classes_count;i++) {
        Class c=all[i];const char *n=class_getName(c);
        if(strncmp(n,"_UIStatusBarVisualProvider",strlen("_UIStatusBarVisualProvider")))continue;
        fprintf(stderr,"marble-ui-layout: provider=%s parent=%s\n",n,class_getName(class_getSuperclass(c)));
        const char *props[]={"nativeDisplayWidth","_nativeScale","referenceScreenScale","_scale","height"};
        for(unsigned j=0;j<5;j++) {
            Method m=class_getClassMethod(c,sel_registerName(props[j]));
            if(m && method_getTypeEncoding(m)[0]=='d')fprintf(stderr,"marble-ui-layout: provider=%s %s=%g\n",n,props[j],SEND(double,c,props[j]));
        }
        if(class_getClassMethod(c,sel_registerName("notchSize"))) {
            CGSize sz=SEND(CGSize,c,"notchSize");
            fprintf(stderr,"marble-ui-layout: provider=%s notch=%gx%g\n",n,sz.width,sz.height);
        }
    }
    free(all);
    const char *classes[]={"_UIStatusBarVisualProvider_iOS","_UIStatusBarVisualProvider_Fallback",
        "_UIStatusBarVisualProvider_Split","_UIStatusBarVisualProvider_FixedSplit",
        "_UIStatusBarVisualProvider_DynamicSplit","_UIStatusBarVisualProvider_Phone","UIScreen"};
    for(unsigned i=0;i<sizeof(classes)/sizeof(classes[0]);i++) {
        methods(classes[i],1);
        if(i<6)methods(classes[i],0);
    }
    const char *wallpapers[]={"SBWallpaperController","SBFWallpaperView","SBWallpaperView","SBWallpaperEffectView","PBUIWallpaperView","SBFWallpaperConfiguration","SBSUIWallpaperService","SBSUIWallpaperOptions"};
    for(unsigned i=0;i<sizeof(wallpapers)/sizeof(wallpapers[0]);i++) {methods(wallpapers[i],0);methods(wallpapers[i],1);}
    CFArrayRef languages=CFLocaleCopyPreferredLanguages();
    if(languages) {char line[1024];CFStringRef d=CFCopyDescription(languages);
        if(CFStringGetCString(d,line,sizeof(line),kCFStringEncodingUTF8))fprintf(stderr,"marble-ui-layout: preferred_languages=%s\n",line);
        CFRelease(d);CFRelease(languages);}
    fflush(stderr);
}
static void reset_wallpaper(void)
{
    Class cls=objc_getClass("SBWallpaperController");
    id controller=SEND(id,cls,"sharedInstance");
    SEL selector=sel_registerName("wallpaperServer:restoreDefaultWallpaperWithCompletionHandler:");
    Method method=class_getInstanceMethod(cls,selector);size_t size=0;
    uintptr_t slide=(uintptr_t)_dyld_get_shared_cache_range(&size)-0x180000000ULL;
    if(!controller || !method || strcmp(method_getTypeEncoding(method),"v32@0:8@16@?24") ||
       (uintptr_t)method_getImplementation(method)-slide!=0x1c4d3e088ULL) {
        fprintf(stderr,"marble-ui-layout: wallpaper_restore_refused abi_or_controller=0\n");return;
    }
    ((void(*)(id,SEL,id,id))objc_msgSend)(controller,selector,NULL,(id)^(BOOL result){
        fprintf(stderr,"marble-ui-layout: wallpaper_restore_completed accepted=%u\n",(unsigned)result);
    });
    fprintf(stderr,"marble-ui-layout: wallpaper_restore_requested native=1 ram_only=1\n");
}
static void set_color_wallpaper(void)
{
    Class cls=objc_getClass("SBWallpaperController");id controller=SEND(id,cls,"sharedInstance");
    SEL selector=sel_registerName("wallpaperServer:setWallpaperColor:darkColor:forVariants:completionHandler:");
    Method method=class_getInstanceMethod(cls,selector);size_t size=0;
    uintptr_t slide=(uintptr_t)_dyld_get_shared_cache_range(&size)-0x180000000ULL;
    if(!controller || !method || strcmp(method_getTypeEncoding(method),"v56@0:8@16@24@32q40@?48") ||
       (uintptr_t)method_getImplementation(method)-slide!=0x1c4d3dd64ULL)return;
    id color=((id(*)(id,SEL,double,double,double,double))objc_msgSend)(objc_getClass("UIColor"),sel_registerName("colorWithRed:green:blue:alpha:"),0.12,0.36,0.62,1.0);
    ((void(*)(id,SEL,id,id,id,long,id))objc_msgSend)(controller,selector,NULL,color,color,3,(id)^(BOOL result){
        fprintf(stderr,"marble-ui-layout: wallpaper_color_completed accepted=%u\n",(unsigned)result);
    });
    fprintf(stderr,"marble-ui-layout: wallpaper_color_requested native=1 ram_only=1\n");
}
static void view_tree_bounded(id view,unsigned depth,unsigned *remaining,unsigned max_depth)
{
    if(!view || depth>max_depth || !*remaining)return;
    --*remaining;
    Class cls=object_getClass(view);
    Method frame=class_getInstanceMethod(cls,sel_registerName("frame"));
    Method bounds=class_getInstanceMethod(cls,sel_registerName("bounds"));
    Method hidden=class_getInstanceMethod(cls,sel_registerName("isHidden"));
    Method alpha=class_getInstanceMethod(cls,sel_registerName("alpha"));
    if(!frame || !bounds || !hidden || !alpha)return;
    CGRect f=SEND(CGRect,view,"frame"),b=SEND(CGRect,view,"bounds");
    fprintf(stderr,"marble-wallpaper-view: depth=%u class=%s frame=%g,%g,%g,%g bounds=%g,%g,%g,%g hidden=%d alpha=%g\n",depth,object_getClassName(view),f.origin.x,f.origin.y,f.size.width,f.size.height,b.origin.x,b.origin.y,b.size.width,b.size.height,(int)SEND(BOOL,view,"isHidden"),SEND(double,view,"alpha"));
    if(!strcmp(getprogname(),"Preferences") && class_getInstanceMethod(cls,sel_registerName("text"))) {
        id text=SEND(id,view,"text");
        if(text && class_getInstanceMethod(object_getClass(text),sel_registerName("UTF8String"))) {
            const char *utf8=SEND(const char *,text,"UTF8String");
            if(utf8)fprintf(stderr,"marble-app-view: depth=%u text=%.240s\n",depth,utf8);
        }
    }
    id children=SEND(id,view,"subviews");unsigned long count=SEND(unsigned long,children,"count");
    for(unsigned long i=0;i<count && i<20 && *remaining;i++)view_tree_bounded(((id(*)(id,SEL,unsigned long))objc_msgSend)(children,sel_registerName("objectAtIndex:"),i),depth+1,remaining,max_depth);
}
static void view_tree(id view,unsigned depth)
{
    unsigned remaining=100;view_tree_bounded(view,depth,&remaining,4);
}
static void app_layer_tree(id layer,unsigned depth,unsigned *remaining)
{
    if(!layer || depth>32 || !*remaining)return;
    --*remaining;CGRect frame=SEND(CGRect,layer,"frame");
    if(depth<=3 || !strcmp(object_getClassName(layer),"CALayerHost"))
        fprintf(stderr,"marble-app-layer: process=%s depth=%u class=%s frame=%g,%g,%g,%g hidden=%d opacity=%g contents=%p\n",getprogname(),depth,object_getClassName(layer),frame.origin.x,frame.origin.y,frame.size.width,frame.size.height,(int)SEND(BOOL,layer,"isHidden"),(double)SEND(float,layer,"opacity"),SEND(void *,layer,"contents"));
    if(!strcmp(object_getClassName(layer),"CALayerHost"))
        fprintf(stderr,"marble-app-layer: host_context=%x inherits_security=%d\n",SEND(unsigned,layer,"contextId"),(int)SEND(BOOL,layer,"inheritsSecurity"));
    id children=SEND(id,layer,"sublayers");unsigned long count=SEND(unsigned long,children,"count");
    for(unsigned long i=0;i<count && i<24 && *remaining;i++)app_layer_tree(((id(*)(id,SEL,unsigned long))objc_msgSend)(children,sel_registerName("objectAtIndex:"),i),depth+1,remaining);
}
static void inspect_app(unsigned at)
{
    id app=SEND(id,objc_getClass("UIApplication"),"sharedApplication");
    if(!app)return;
    id windows=SEND(id,app,"windows"),scenes=SEND(id,app,"connectedScenes");
    unsigned long nw=SEND(unsigned long,windows,"count"),ns=SEND(unsigned long,scenes,"count");
    fprintf(stderr,"marble-app-view: t=%u state=%ld windows=%lu scenes=%lu main_thread=%d\n",at,SEND(long,app,"applicationState"),nw,ns,pthread_main_np());
    id all=SEND(id,scenes,"allObjects");
    for(unsigned long i=0;i<ns && i<8;i++) {
        id scene=((id(*)(id,SEL,unsigned long))objc_msgSend)(all,sel_registerName("objectAtIndex:"),i);
        fprintf(stderr,"marble-app-view: scene=%lu class=%s activation=%ld\n",i,object_getClassName(scene),SEND(long,scene,"activationState"));
    }
    unsigned remaining=240;
    for(unsigned long i=0;i<nw && i<32;i++) {
        id window=((id(*)(id,SEL,unsigned long))objc_msgSend)(windows,sel_registerName("objectAtIndex:"),i);
        id controller=SEND(id,window,"rootViewController");
        fprintf(stderr,"marble-app-view: window=%lu class=%s key=%d level=%g root=%s loaded=%d\n",i,object_getClassName(window),(int)SEND(BOOL,window,"isKeyWindow"),SEND(double,window,"windowLevel"),controller?object_getClassName(controller):"nil",controller?(int)SEND(BOOL,controller,"isViewLoaded"):0);
        if(class_getInstanceMethod(object_getClass(window),sel_registerName("_boundContext"))) {
            id context=SEND(id,window,"_boundContext");
            if(context)fprintf(stderr,"marble-app-layer: process=%s window=%lu context=%x secure=%d commit=%llu\n",getprogname(),i,SEND(unsigned,context,"contextId"),(int)SEND(BOOL,context,"isSecure"),SEND(unsigned long long,context,"commitId"));
        }
        if(at==5)view_tree_bounded(window,0,&remaining,12);
        if(!strcmp(getprogname(),"Preferences") || !strcmp(object_getClassName(window),"SBMainSwitcherWindow") || !strcmp(object_getClassName(window),"SBRootSceneWindow")) {
            unsigned layer_remaining=480;
            app_layer_tree(SEND(id,window,"layer"),0,&layer_remaining);
            fprintf(stderr,"marble-app-layer: process=%s window=%lu visited=%u budget=480\n",getprogname(),i,480-layer_remaining);
        }
    }
    fprintf(stderr,"marble-app-view: t=%u nodes=%u budget=240\n",at,240-remaining);fflush(stderr);
}
static void inspect_wallpaper(void)
{
    Class cls=objc_getClass("SBWallpaperController");
    Method shared=class_getClassMethod(cls,sel_registerName("sharedInstance"));
    Method get=class_getInstanceMethod(cls,sel_registerName("_wallpaperViewForVariant:"));
    size_t size=0;uintptr_t base=(uintptr_t)_dyld_get_shared_cache_range(&size),slide=base-0x180000000ULL;
    if(!base || !shared || !get || strcmp(method_getTypeEncoding(shared),"@16@0:8") ||
       strcmp(method_getTypeEncoding(get),"@24@0:8q16") ||
       (uintptr_t)method_getImplementation(shared)-slide!=0x1c4a1ae70ULL ||
       (uintptr_t)method_getImplementation(get)-slide!=0x1c4a46bb8ULL) {
        fprintf(stderr,"marble-wallpaper-view: refused controller_abi=0\n");return;
    }
    id controller=SEND(id,cls,"sharedInstance");
    for(long variant=0;variant<2;variant++) {
        id view=((id(*)(id,SEL,long))objc_msgSend)(controller,sel_registerName("_wallpaperViewForVariant:"),variant);
        fprintf(stderr,"marble-wallpaper-view: variant=%ld class=%s native_container=1\n",variant,view?object_getClassName(view):"nil");
        view_tree(view,0);
    }
    Method window=class_getInstanceMethod(cls,sel_registerName("_window"));
    if(window && !strcmp(method_getTypeEncoding(window),"@16@0:8") && (uintptr_t)method_getImplementation(window)-slide==0x1c4a5f338ULL)
        view_tree(SEND(id,controller,"_window"),0);
    fflush(stderr);
}
static void static_wallpaper(void)
{
    /* The 20F66 Poster pipeline creates a secure window but no variant
     * wallpaper views on this RAM system. A static image in that native
     * wallpaper window is an explicit compatibility fallback, below the
     * separate icon/application windows and available to backdrop sampling. */
    Class cls=objc_getClass("SBWallpaperController");
    Method shared=class_getClassMethod(cls,sel_registerName("sharedInstance"));
    Method get=class_getInstanceMethod(cls,sel_registerName("_window"));
    size_t size=0;uintptr_t base=(uintptr_t)_dyld_get_shared_cache_range(&size),slide=base-0x180000000ULL;
    if(!base || !shared || !get || strcmp(method_getTypeEncoding(shared),"@16@0:8") ||
       strcmp(method_getTypeEncoding(get),"@16@0:8") ||
       (uintptr_t)method_getImplementation(shared)-slide!=0x1c4a1ae70ULL ||
       (uintptr_t)method_getImplementation(get)-slide!=0x1c4a5f338ULL)return;
    id controller=SEND(id,cls,"sharedInstance"),window=SEND(id,controller,"_window");
    if(!window || strcmp(object_getClassName(window),"_SBWallpaperSecureWindow"))return;
    CGRect bounds=SEND(CGRect,window,"bounds");
    if(fabs(bounds.size.width-414)>0.01 || fabs(bounds.size.height-896)>0.01)return;
    static id picture;
    if(picture && SEND(id,picture,"superview")==window)return;
    if(picture) {SEND(void,picture,"removeFromSuperview");SEND(void,picture,"release");picture=NULL;}
    id path=((id(*)(id,SEL,const char *))objc_msgSend)(objc_getClass("NSString"),sel_registerName("stringWithUTF8String:"),"/redwood/share/wallpaper-static.png");
    id image=((id(*)(id,SEL,id))objc_msgSend)(objc_getClass("UIImage"),sel_registerName("imageWithContentsOfFile:"),path);
    if(!image) {fprintf(stderr,"marble-wallpaper-static: image_decode_failed\n");return;}
    picture=((id(*)(id,SEL,id))objc_msgSend)(SEND(id,objc_getClass("UIImageView"),"alloc"),sel_registerName("initWithImage:"),image);
    if(!picture)return;
    ((void(*)(id,SEL,CGRect))objc_msgSend)(picture,sel_registerName("setFrame:"),bounds);
    ((void(*)(id,SEL,long))objc_msgSend)(picture,sel_registerName("setContentMode:"),2);
    ((void(*)(id,SEL,unsigned long))objc_msgSend)(picture,sel_registerName("setAutoresizingMask:"),18);
    ((void(*)(id,SEL,BOOL))objc_msgSend)(picture,sel_registerName("setUserInteractionEnabled:"),NO);
    ((void(*)(id,SEL,BOOL))objc_msgSend)(picture,sel_registerName("setClipsToBounds:"),YES);
    ((void(*)(id,SEL,id))objc_msgSend)(window,sel_registerName("addSubview:"),picture);
    fprintf(stderr,"marble-wallpaper-static: attached native_window=%s bounds=%gx%g static_fallback=1\n",object_getClassName(window),bounds.size.width,bounds.size.height);
}
static void test_open_settings(void)
{
    const char *classes[]={"SBIconController","SBIconModel","SBApplicationIcon"};
    for(unsigned c=0;c<3;c++)for(unsigned meta=0;meta<2;meta++) {
        Class cls=objc_getClass(classes[c]);if(!cls)continue;
        if(meta)cls=object_getClass((id)cls);
        unsigned count=0;Method *list=class_copyMethodList(cls,&count);
        size_t bytes=0;uintptr_t slide=(uintptr_t)_dyld_get_shared_cache_range(&bytes)-0x180000000ULL;
        for(unsigned i=0;i<count;i++) {
            const char *name=sel_getName(method_getName(list[i]));
            if(strstr(name,"launchIcon") || strstr(name,"LaunchIcon") || strstr(name,"iconFor") || strstr(name,"IconFor") || !strcmp(name,"sharedInstance") || !strcmp(name,"model"))
                fprintf(stderr,"marble-ui-icon: %c[%s %s] type=%s imp=%#llx\n",meta?'+':'-',classes[c],name,method_getTypeEncoding(list[i]),(unsigned long long)((uintptr_t)method_getImplementation(list[i])-slide));
        }
        free(list);
    }
    id app=SEND(id,objc_getClass("UIApplication"),"sharedApplication");
    const char *names[]={"launchApplicationWithIdentifier:suspended:","_launchApplicationWithIdentifier:suspended:"};
    for(unsigned i=0;i<2;i++) {
        SEL selector=sel_registerName(names[i]);Method method=class_getInstanceMethod(object_getClass(app),selector);
        if(!method)continue;
        const char *type=method_getTypeEncoding(method);
        fprintf(stderr,"marble-ui-launch: receiver=%s selector=%s type=%s\n",object_getClassName(app),names[i],type);
        if(strcmp(type,"B28@0:8@16B24") && strcmp(type,"c28@0:8@16c24"))continue;
        id bundle=((id(*)(id,SEL,const char *))objc_msgSend)(objc_getClass("NSString"),sel_registerName("stringWithUTF8String:"),"com.apple.Preferences");
        BOOL accepted=((BOOL(*)(id,SEL,id,BOOL))objc_msgSend)(app,selector,bundle,NO);
        fprintf(stderr,"marble-ui-launch: Preferences requested accepted=%d native_springboard=1\n",(int)accepted);return;
    }
    fprintf(stderr,"marble-ui-launch: no_qualified_native_launcher\n");
}
static thread_t sampled_main;
static uintptr_t sampled_stack_low,sampled_stack_high;
static void sample_main(unsigned at)
{
    arm_thread_state64_t state;mach_msg_type_number_t count=ARM_THREAD_STATE64_COUNT;
    kern_return_t status=thread_get_state(sampled_main,ARM_THREAD_STATE64,(thread_state_t)&state,&count);
    if(status!=KERN_SUCCESS) {fprintf(stderr,"marble-app-wait: t=%u thread_state_rc=%d\n",at,status);return;}
    size_t bytes=0;uintptr_t base=(uintptr_t)_dyld_get_shared_cache_range(&bytes),slide=base-0x180000000ULL;
    uintptr_t pc=state.__pc,fp=state.__fp;
    fprintf(stderr,"marble-app-wait: t=%u pc=%#llx cache_pc=%#llx sp=%#llx frames=",at,(unsigned long long)pc,
        (unsigned long long)(pc>=base && pc-base<bytes?pc-slide:0),(unsigned long long)state.__sp);
    for(unsigned i=0;i<24;i++) {
        uint64_t pair[2];mach_vm_size_t read=0;
        if((fp&15) || fp<sampled_stack_low || fp>sampled_stack_high-16)break;
        if(mach_vm_read_overwrite(mach_task_self(),fp,16,(mach_vm_address_t)pair,&read)!=KERN_SUCCESS || read!=16)break;
        uintptr_t ret=pair[1];
        fprintf(stderr," %#llx",(unsigned long long)(ret>=base && ret-base<bytes?ret-slide:ret));
        if(pair[0]<=fp)break;fp=pair[0];
    }
    fprintf(stderr," unsuspended_sample=1\n");fflush(stderr);
}
static void inspect_statusbar(void);
static unsigned statusbar_inspected,statusbar_mutated_nodes;
static void inspect_timezone(void)
{
    time_t now=time(NULL);struct tm local={0},utc={0};
    localtime_r(&now,&local);gmtime_r(&now,&utc);
    id zone=SEND(id,objc_getClass("NSTimeZone"),"systemTimeZone");
    id name=zone?SEND(id,zone,"name"):NULL;
    fprintf(stderr,"marble-timezone: epoch=%lld local=%04d-%02d-%02dT%02d:%02d utc=%02d:%02d CF_zone=%s CF_offset=%ld\n",(long long)now,local.tm_year+1900,local.tm_mon+1,local.tm_mday,local.tm_hour,local.tm_min,utc.tm_hour,utc.tm_min,name?SEND(const char *,name,"UTF8String"):"nil",zone?SEND(long,zone,"secondsFromGMT"):0);
    const char *paths[]={"/var/db/timezone/localtime","/etc/localtime","/usr/share/zoneinfo.default/Asia/Shanghai"};
    for(unsigned i=0;i<3;i++) {
        char target[256]={0},magic[5]={0};ssize_t n=readlink(paths[i],target,sizeof(target)-1);
        int fd=open(paths[i],O_RDONLY);ssize_t got=fd>=0?read(fd,magic,4):-1;if(fd>=0)close(fd);
        fprintf(stderr,"marble-timezone: path=%s link=%s read=%ld magic=%.4s\n",paths[i],n>=0?target:"none",(long)got,magic);
    }
}
static void install_ready_signal(void)
{
    if(strcmp(getprogname(),"SpringBoard") || access("/redwood/etc/ui-ready-gate",F_OK))return;
    id center=SEND(id,objc_getClass("NSNotificationCenter"),"defaultCenter");
    const id *name=(const id *)dlsym(RTLD_DEFAULT,"UIApplicationDidFinishLaunchingNotification");
    if(!center || !name || !*name) {fprintf(stderr,"marble-ui-ready: notification_unavailable\n");return;}
    static int token;
    uint32_t rc=notify_register_check("com.marble.ui.springboard-ready",&token);
    if(rc!=NOTIFY_STATUS_OK)return;
    notify_set_state(token,0);
    static id observer;
    observer=((id(*)(id,SEL,id,id,id,void(^)(id)))objc_msgSend)(center,
        sel_registerName("addObserverForName:object:queue:usingBlock:"),*name,NULL,
        SEND(id,objc_getClass("NSOperationQueue"),"mainQueue"),^(id notification){
            (void)notification;
            uint32_t status=notify_set_state(token,1);
            if(status==NOTIFY_STATUS_OK)status=notify_post("com.marble.ui.springboard-ready");
            fprintf(stderr,"marble-ui-ready: SpringBoard_did_finish_launch=1 status=%u main_queue=1\n",status);
            inspect_timezone();
            if(!access("/redwood/etc/ui-statusbar-inspect",F_OK) || !access("/redwood/etc/ui-statusbar-gradient-alpha",F_OK))
                dispatch_after(dispatch_time(DISPATCH_TIME_NOW,2*NSEC_PER_SEC),dispatch_get_main_queue(),^{inspect_statusbar();});
        });
    fprintf(stderr,"marble-ui-ready: observer_registered=%d\n",observer!=NULL);
}
static void statusbar_node(id view,unsigned depth,unsigned *budget,unsigned inherited)
{
    if(!view || depth>16 || !*budget)return;--*budget;
    const char *name=object_getClassName(view);
    CGRect frame=SEND(CGRect,view,"frame");
    unsigned selected=inherited || strstr(name,"StatusBar")!=NULL;
    if(!selected && !SEND(BOOL,view,"isHidden") && SEND(double,view,"alpha")>0 &&
       frame.origin.x>=0 && frame.origin.x<120 && frame.origin.y>=0 && frame.origin.y<60 &&
       frame.size.width>=30 && frame.size.width<=230 && frame.size.height>=10 && frame.size.height<=110)
        selected=1; /* Read-only candidates; never change an unnamed node. */
    if(selected) {
        CGRect f=SEND(CGRect,view,"frame");id color=SEND(id,view,"backgroundColor"),layer=SEND(id,view,"layer");
        CGColorRef bg=color?SEND(CGColorRef,color,"CGColor"):NULL;
        CGColorRef lb=SEND(CGColorRef,layer,"backgroundColor");
        fprintf(stderr,"marble-statusbar: depth=%u class=%s frame=%g,%g,%g,%g hidden=%d alpha=%g opaque=%d bg_alpha=%g layer_bg_alpha=%g layer_class=%s layer_opaque=%d contents=%p\n",depth,name,f.origin.x,f.origin.y,f.size.width,f.size.height,(int)SEND(BOOL,view,"isHidden"),SEND(double,view,"alpha"),(int)SEND(BOOL,view,"isOpaque"),bg?CGColorGetAlpha(bg):-1.0,lb?CGColorGetAlpha(lb):-1.0,object_getClassName(layer),(int)SEND(BOOL,layer,"isOpaque"),SEND(void *,layer,"contents"));
        if(!strcmp(name,"SBStatusBarLegibilityGradientView") && !strcmp(object_getClassName(layer),"CAGradientLayer")) {
            id colors=SEND(id,layer,"colors");unsigned long n=SEND(unsigned long,colors,"count");
            /* Native 20F66 uses 31 stops. Inspect every stop before touching
             * opacity; a truncated prefix cannot qualify the gradient. */
            double min_alpha=1,max_alpha=0;unsigned valid=n>0 && n<=64;
            for(unsigned long i=0;i<n && i<64;i++) {
                CGColorRef c=((CGColorRef(*)(id,SEL,unsigned long))objc_msgSend)(colors,sel_registerName("objectAtIndex:"),i);
                if(!c || CFGetTypeID(c)!=CGColorGetTypeID()) {valid=0;break;}
                double a=CGColorGetAlpha(c);if(a<min_alpha)min_alpha=a;if(a>max_alpha)max_alpha=a;
            }
            fprintf(stderr,"marble-statusbar-gradient: colors=%lu valid=%u alpha_min=%g alpha_max=%g\n",n,valid,min_alpha,max_alpha);
            if(valid && min_alpha<.999 && !bg && !lb && f.origin.x==0 && f.origin.y==0 &&
               f.size.width==414 && f.size.height==100 && !access("/redwood/etc/ui-statusbar-gradient-alpha",F_OK)) {
                ((void(*)(id,SEL,BOOL))objc_msgSend)(view,sel_registerName("setOpaque:"),NO);
                ((void(*)(id,SEL,BOOL))objc_msgSend)(layer,sel_registerName("setOpaque:"),NO);
                statusbar_mutated_nodes++;
                id parent=SEND(id,view,"superview");
                if(parent && !strcmp(object_getClassName(parent),"SBFStatusBarLegibilityView") && !SEND(id,parent,"backgroundColor")) {
                    ((void(*)(id,SEL,BOOL))objc_msgSend)(parent,sel_registerName("setOpaque:"),NO);
                    statusbar_mutated_nodes++;
                }
                fprintf(stderr,"marble-statusbar-gradient: transparent_gradient_opaque_corrected=1 colors_and_geometry_unchanged=1\n");
            }
        }
    }
    id children=SEND(id,view,"subviews");unsigned long count=SEND(unsigned long,children,"count");
    for(unsigned long i=0;i<count && i<24 && *budget;i++)statusbar_node(((id(*)(id,SEL,unsigned long))objc_msgSend)(children,sel_registerName("objectAtIndex:"),i),depth+1,budget,selected);
}
static void inspect_statusbar(void)
{
    if(statusbar_inspected++)return;
    id windows=SEND(id,SEND(id,objc_getClass("UIApplication"),"sharedApplication"),"windows");
    unsigned long count=SEND(unsigned long,windows,"count");
    for(unsigned long i=0;i<count && i<32;i++) {
        id window=((id(*)(id,SEL,unsigned long))objc_msgSend)(windows,sel_registerName("objectAtIndex:"),i);
        fprintf(stderr,"marble-statusbar: window=%s hidden=%d level=%g\n",object_getClassName(window),(int)SEND(BOOL,window,"isHidden"),SEND(double,window,"windowLevel"));
        /* Give the actual status-bar window priority over the large icon tree. */
        if(strstr(object_getClassName(window),"StatusBar")) {unsigned n=120;statusbar_node(window,0,&n,1);}
        else {unsigned n=600;statusbar_node(window,0,&n,0);}
    }
    fprintf(stderr,"marble-statusbar: inspection_complete mutated_nodes=%u\n",statusbar_mutated_nodes);
}
static void install_control_local_pan(void)
{
    if(strcmp(getprogname(),"SpringBoard") || access("/redwood/etc/ui-control-local-pan",F_OK))return;
    static int token;static dispatch_source_t timer;static mach_timebase_info_data_t tb;
    if(notify_register_check(MARBLE_CONTROL_VISIBILITY,&token)!=NOTIFY_STATUS_OK || mach_timebase_info(&tb)!=KERN_SUCCESS || !tb.denom)return;
    notify_set_state(token,0);
    timer=dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER,0,0,dispatch_get_main_queue());if(!timer)return;
    dispatch_source_set_timer(timer,dispatch_time(DISPATCH_TIME_NOW,NSEC_PER_SEC),NSEC_PER_SEC/4,NSEC_PER_SEC/20);
    dispatch_source_set_event_handler(timer,^{
        id app=SEND(id,objc_getClass("UIApplication"),"sharedApplication"),windows=SEND(id,app,"windows");
        unsigned long count=SEND(unsigned long,windows,"count");unsigned visible=0;
        for(unsigned long i=0;i<count && i<32;i++) {
            id w=((id(*)(id,SEL,unsigned long))objc_msgSend)(windows,sel_registerName("objectAtIndex:"),i);
            if(!strcmp(object_getClassName(w),"SBControlCenterWindow") && !SEND(BOOL,w,"isHidden") && SEND(double,w,"alpha")>.99)visible=1;
        }
        uint64_t us=mach_absolute_time()/tb.denom*tb.numer/1000;
        uint32_t rc=notify_set_state(token,(us<<1)|visible);
        static unsigned last=2,traces;
        if(visible!=last && traces++<16)fprintf(stderr,"marble-control-visibility: visible=%u status=%u main_queue=1 fresh_us=%llu\n",visible,rc,(unsigned long long)us);
        last=visible;
    });
    dispatch_resume(timer);
}
static void (*original_window_event)(id,SEL,id);
static void (*original_cc_state)(id,SEL,long);
static unsigned control_event_traces;
static unsigned control_state_traces;
static void control_state(id self,SEL selector,long state)
{
    if(state!=2 && control_state_traces++<24) {
        CGPoint translation={0},velocity={0};Class cls=object_getClass(self);
        Method t=class_getInstanceMethod(cls,sel_registerName("translationInView:"));
        Method v=class_getInstanceMethod(cls,sel_registerName("velocityInView:"));
        if(t && !strcmp(method_getTypeEncoding(t),"{CGPoint=dd}24@0:8@16"))
            translation=((CGPoint(*)(id,SEL,id))objc_msgSend)(self,sel_registerName("translationInView:"),NULL);
        if(v && !strcmp(method_getTypeEncoding(v),"{CGPoint=dd}24@0:8@16"))
            velocity=((CGPoint(*)(id,SEL,id))objc_msgSend)(self,sel_registerName("velocityInView:"),NULL);
        fprintf(stderr,"marble-control-state: old=%ld new=%ld translation=%g,%g velocity=%g,%g original_policy_preserved=1\n",SEND(long,self,"state"),state,translation.x,translation.y,velocity.x,velocity.y);
    }
    original_cc_state(self,selector,state);
}
static void window_event(id self,SEL selector,id event)
{
    if(control_event_traces>=48 || strcmp(object_getClassName(self),"SBControlCenterWindow") || SEND(long,event,"type")!=0) {
        original_window_event(self,selector,event);return;
    }
    id touches=SEND(id,SEND(id,event,"allTouches"),"allObjects");unsigned long count=SEND(unsigned long,touches,"count");
    for(unsigned long i=0;i<count && i<10;i++) {
        id touch=((id(*)(id,SEL,unsigned long))objc_msgSend)(touches,sel_registerName("objectAtIndex:"),i);
        long phase=SEND(long,touch,"phase");if(phase!=0 && phase!=3 && phase!=4)continue;
        control_event_traces++;
        CGPoint p=((CGPoint(*)(id,SEL,id))objc_msgSend)(touch,sel_registerName("locationInView:"),self);
        fprintf(stderr,"marble-control-input: phase=%ld xy=%g,%g timestamp=%g event_timestamp=%g before_original=1\n",phase,p.x,p.y,SEND(double,touch,"timestamp"),SEND(double,event,"timestamp"));
        id recognizers=SEND(id,touch,"gestureRecognizers");unsigned long n=SEND(unsigned long,recognizers,"count");
        for(unsigned long j=0;j<n && j<24;j++) {
            id r=((id(*)(id,SEL,unsigned long))objc_msgSend)(recognizers,sel_registerName("objectAtIndex:"),j);
            fprintf(stderr,"marble-control-recognizer: class=%s state=%ld enabled=%d phase=%ld\n",object_getClassName(r),SEND(long,r,"state"),(int)SEND(BOOL,r,"isEnabled"),phase);
        }
    }
    original_window_event(self,selector,event);
}
static void install_gesture_inspection(void)
{
    if(strcmp(getprogname(),"SpringBoard") || access("/redwood/etc/ui-gesture-inspect",F_OK))return;
    Method m=class_getInstanceMethod(objc_getClass("UIWindow"),sel_registerName("sendEvent:"));
    if(!m || strcmp(method_getTypeEncoding(m),"v24@0:8@16"))return;
    original_window_event=(void *)method_setImplementation(m,(IMP)window_event);
    fprintf(stderr,"marble-control-input: UIWindow_observer_installed=1 dispatch_unchanged=1\n");
    Class cc=objc_getClass("CCUIDismissalGestureRecognizer");SEL state=sel_registerName("setState:");
    Method s=class_getInstanceMethod(cc,state);
    if(s && !strcmp(method_getTypeEncoding(s),"v24@0:8q16")) {
        original_cc_state=(void *)method_getImplementation(s);
        unsigned installed=class_addMethod(cc,state,(IMP)control_state,method_getTypeEncoding(s));
        if(!installed) {
            unsigned count=0;Method *own=class_copyMethodList(cc,&count);
            for(unsigned i=0;i<count;i++)if(method_getName(own[i])==state) {
                method_setImplementation(own[i],(IMP)control_state);installed=1;break;
            }
            free(own);
        }
        fprintf(stderr,"marble-control-state: observer_installed=%u parent=%s\n",installed,class_getName(class_getSuperclass(cc)));
        methods("CCUIDismissalGestureRecognizer",0);
    }
}
__attribute__((constructor)) static void start(void)
{
    install_control_local_pan();
    install_gesture_inspection();
    install_ready_signal();
    if(!strcmp(getprogname(),"SpringBoard") && (!access("/redwood/etc/ui-statusbar-inspect",F_OK) || !access("/redwood/etc/ui-statusbar-gradient-alpha",F_OK)))
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW,38*NSEC_PER_SEC),dispatch_get_main_queue(),^{inspect_statusbar();});
    if(!strcmp(getprogname(),"Preferences") && !access("/redwood/etc/ui-app-wait",F_OK)) {
        int console=open("/dev/console",O_WRONLY);
        if(console>=0) {dup2(console,2);close(console);}
        fprintf(stderr,"marble-app-wait: process=Preferences constructor=1\n");
        sampled_main=mach_thread_self();
        sampled_stack_high=(uintptr_t)pthread_get_stackaddr_np(pthread_self());
        sampled_stack_low=sampled_stack_high-pthread_get_stacksize_np(pthread_self());
        const unsigned times[]={2,5,8,12,18};
        for(unsigned i=0;i<5;i++) {
            unsigned at=times[i];
            dispatch_after(dispatch_time(DISPATCH_TIME_NOW,at*NSEC_PER_SEC),dispatch_get_global_queue(QOS_CLASS_UTILITY,0),^{sample_main(at);});
            dispatch_after(dispatch_time(DISPATCH_TIME_NOW,at*NSEC_PER_SEC),dispatch_get_main_queue(),^{fprintf(stderr,"marble-app-wait: t=%u main_queue_responsive=1\n",at);if(at==5 || at==12)inspect_app(at);});
        }
    }
    install_geometry();
    if(!strcmp(getprogname(),"SpringBoard") && !access("/redwood/etc/ui-open-settings",F_OK)) {
        int token;
        notify_register_dispatch("com.marble.test.open-settings",&token,dispatch_get_main_queue(),^(int t){(void)t;test_open_settings();});
        if(!access("/redwood/etc/ui-app-wait",F_OK)) {
            int layer_token;
            notify_register_dispatch("com.marble.test.layers",&layer_token,dispatch_get_main_queue(),^(int t){(void)t;inspect_app(12);});
        }
    }
    if(!strcmp(getprogname(),"SpringBoard") && !access("/redwood/etc/ui-wallpaper-static",F_OK))
        for(unsigned t=25;t<=65;t+=20)
            dispatch_after(dispatch_time(DISPATCH_TIME_NOW,t*NSEC_PER_SEC),dispatch_get_main_queue(),^{static_wallpaper();});
    if(!strcmp(getprogname(),"SpringBoard") && !access("/redwood/etc/ui-wallpaper-inspect",F_OK))
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW,32*NSEC_PER_SEC),dispatch_get_main_queue(),^{inspect_wallpaper();});
    if(!strcmp(getprogname(),"SpringBoard") && !access("/redwood/etc/ui-wallpaper-color",F_OK))
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW,30*NSEC_PER_SEC),dispatch_get_main_queue(),^{set_color_wallpaper();});
    else if(!strcmp(getprogname(),"SpringBoard") && !access("/redwood/etc/ui-wallpaper-reset",F_OK))
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW,30*NSEC_PER_SEC),dispatch_get_main_queue(),^{reset_wallpaper();});
    if(!getenv("MARBLE_UI_LAYOUT"))return;
    fprintf(stderr,"marble-ui-layout: process=%s diagnostic_only=1\n",getprogname());
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW,35*NSEC_PER_SEC),dispatch_get_main_queue(),^{screen();});
}
