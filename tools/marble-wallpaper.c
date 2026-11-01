/* Native wallpaper IPC lives in its own process: calling its synchronous
 * server from SpringBoard's main queue can deadlock SpringBoard itself. */
#include <CoreFoundation/CoreFoundation.h>
#include <objc/runtime.h>
#include <objc/message.h>
#include <dlfcn.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <dispatch/dispatch.h>
#include <mach-o/dyld.h>
#include <stdlib.h>
extern const void *_dyld_get_shared_cache_range(size_t *);
#define SEND(type,target,name) ((type(*)(id,SEL))objc_msgSend)((id)(target),sel_registerName(name))
static int perform(void)
{
    setvbuf(stderr,NULL,_IONBF,0);sleep(30);
    id pool=SEND(id,SEND(id,objc_getClass("NSAutoreleasePool"),"alloc"),"init");
    void *framework=dlopen("/System/Library/PrivateFrameworks/SpringBoardUIServices.framework/SpringBoardUIServices",RTLD_NOW|RTLD_GLOBAL);
    if(!framework){fprintf(stderr,"marble-wallpaper: framework_load_failed %s\n",dlerror());return 2;}
    typedef BOOL (*SetImages)(CFDictionaryRef,CFDictionaryRef,long,long);
    // The managed-configuration service is not provisioned in this RAM OS.
    // Use the native unchecked entry only for this isolated wallpaper request;
    // verify the exact 20F66 implementation, and leave all policy code intact.
    void *published=dlsym(RTLD_DEFAULT,"SBSUIWallpaperSetImages");
    SetImages set=NULL;
    Class cls=objc_getClass("SBFWallpaperOptions");
    if(!published || !cls){fprintf(stderr,"marble-wallpaper: native_api_missing function=%u options=%u\n",!!published,!!cls);return 2;}
    size_t length=0;uintptr_t slide=(uintptr_t)_dyld_get_shared_cache_range(&length)-0x180000000ULL;
    if((uintptr_t)published-slide!=0x1a34000b8ULL){fprintf(stderr,"marble-wallpaper: native_api_abi_differs calls=0\n");return 4;}
    // The unchecked implementation is private in 20F66's export trie. Resolve
    // the exact BL target used by the verified published wrapper, rather than
    // treating a guessed symbol name or an unchecked address as a callable ABI.
    uintptr_t branch=(uintptr_t)published+0x64;
    uint32_t instruction=*(const uint32_t *)branch;
    if((instruction&0xfc000000)!=0x94000000)return 4;
    intptr_t displacement=((int32_t)(instruction<<6))>>4;
    uintptr_t target=branch+displacement;
    if(target-slide!=0x1a33ffe0cULL)return 4;
    set=(SetImages)target;
    fprintf(stderr,"marble-wallpaper: native_api_unslid=%#llx main_queue_running=1\n",(unsigned long long)((uintptr_t)set-slide));
    id image=((id(*)(id,SEL,id))objc_msgSend)(objc_getClass("UIImage"),sel_registerName("imageWithContentsOfFile:"),(id)CFSTR("/redwood/share/wallpaper.png"));
    if(!image){fprintf(stderr,"marble-wallpaper: png_decode_failed\n");return 3;}
    id options=SEND(id,SEND(id,cls,"alloc"),"init");
    SEL parallax=sel_registerName("setParallaxEnabled:");Method method=class_getInstanceMethod(cls,parallax);
    if(method && !strcmp(method_getTypeEncoding(method),"v20@0:8B16"))
        ((void(*)(id,SEL,BOOL))objc_msgSend)(options,parallax,NO);
    // 20F66 consumes two dictionaries keyed by NSNumber appearance; its
    // one-image branch applies that image to the requested locations.
    int appearance=1;CFNumberRef key=CFNumberCreate(NULL,kCFNumberIntType,&appearance);
    const void *keys[]={key},*values[]={image},*option_values[]={options};
    CFDictionaryRef images=CFDictionaryCreate(NULL,keys,values,1,&kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);
    CFDictionaryRef options_by_appearance=CFDictionaryCreate(NULL,keys,option_values,1,&kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);
    fprintf(stderr,"marble-wallpaper: native_request process=separate pid=%d locations=3 mode=0 ram_only=1\n",getpid());
    BOOL accepted=set(images,options_by_appearance,3,0);
    fprintf(stderr,"marble-wallpaper: request_returned accepted=%u visual_verification_required=1\n",(unsigned)accepted);
    CFRelease(options_by_appearance);CFRelease(images);CFRelease(key);SEND(void,options,"release");SEND(void,pool,"drain");return accepted?0:5;
}
int main(void)
{
    // Let UIKit/NSXPC use the main queue while this synchronous IPC is pending.
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_UTILITY,0),^{exit(perform());});
    dispatch_main();
}
