/* Exact supplied Mu GlinkDxe 9fbbf72b... channel lifecycle and isolated
 * AOP load_state TX. Never writes raw QMP/MMIO, calls RPM teardown, NPA,
 * PAS, or storage. Static client
 * state outlives every native callback, including uncertain close outcomes. */
#ifndef MARBLE_QMP_NATIVE_FW_H
#define MARBLE_QMP_NATIVE_FW_H
#ifdef XNU_MARBLE
#include "marble_code_digest.h"
#include "marble_qmp_code.h"
typedef struct {
    const char *transport,*remote,*name;
    uint32_t options,pad;
    const void *priv;
    void *rx,*rx_tracer;
    void (EFIAPI *rxv)(void *,const void *,const void *,void *,size_t,size_t,void *,void *);
    void (EFIAPI *tx_done)(void *,const void *,const void *,const void *,size_t);
    void (EFIAPI *state)(void *,const void *,unsigned);
    void *intent_req,*intent,*signals,*rx_abort,*tx_abort;
    uint32_t timeout,pad2;
    void *allocate,*deallocate;
} MarbleQmpOpenConfig;
_Static_assert(sizeof(MarbleQmpOpenConfig)==144,"native GLink open ABI");
_Static_assert(offsetof(MarbleQmpOpenConfig,state)==0x48,"state callback ABI");
_Static_assert(offsetof(MarbleQmpOpenConfig,rxv)==0x38,"vector callback ABI");
static struct {
    unsigned attempted,dirty,active,connected,local_down,remote_down,invalid,rx,tx,closing;
    void *observed;
    unsigned tx_calls,tx_pending,tx_index,tx_acks;
    uint32_t tx_cookie[2];
    MarbleQmpOpenConfig config;
} marble_qmp_native;
/* Same 64-byte, zero-filled business messages as pinned marble Linux PAS.
 * Static storage remains valid even after an uncertain native completion. */
static const char marble_qmp_load_message[2][64] __attribute__((aligned(4)))={
    "{class: image, res: load_state, name: adsp, val: on}",
    "{class: image, res: load_state, name: adsp, val: off}"
};
static void EFIAPI marble_qmp_native_state(void *h,const void *p,unsigned event)
{
    if(p!=&marble_qmp_native || !h || (marble_qmp_native.observed && marble_qmp_native.observed!=h)) {
        marble_qmp_native.invalid++;return;
    }
    marble_qmp_native.observed=h;
    if(event==0 && !marble_qmp_native.closing)marble_qmp_native.connected++;
    else if(event==1 && marble_qmp_native.closing)marble_qmp_native.local_down++;
    else if(event==2)marble_qmp_native.remote_down++;
    else marble_qmp_native.invalid++;
}
static void EFIAPI marble_qmp_native_rxv(void *h,const void *p,const void *packet,void *v,
    size_t bytes,size_t used,void *vp,void *pp)
{
    /* No pointer dereference or nested firmware API from this callback. This
     * no-TX experiment expects no received data; anything arriving fails it. */
    (void)h;(void)p;(void)packet;(void)v;(void)bytes;(void)used;(void)vp;(void)pp;
    marble_qmp_native.rx++;
}
static void EFIAPI marble_qmp_native_tx_done(void *h,const void *p,const void *packet,const void *data,size_t bytes)
{
    unsigned index=marble_qmp_native.tx_index;
    if(!marble_qmp_native.tx_pending || index>1 || h!=marble_qmp_native.observed ||
       p!=&marble_qmp_native || packet!=&marble_qmp_native.tx_cookie[index] ||
       data!=marble_qmp_load_message[index] || bytes!=64 || marble_qmp_native.tx!=index) {
        marble_qmp_native.invalid++;return;
    }
    marble_qmp_native.tx++;marble_qmp_native.tx_pending=0;
}
static void EFIAPI marble_qmp_native_tx_abort(void *h,const void *p,const void *packet)
{(void)h;(void)p;(void)packet;marble_qmp_native.invalid++;}
typedef struct {
    uint64_t transport,core,first;
    unsigned low_state,core_state,count,local,remote,refs,client_empty;
} MarbleQmpNativeContext;
static int marble_qmp_native_context(uint64_t base,MarbleQmpNativeContext *c,const char *phase)
{
    uint64_t t=0,v=0,core=0,first=0,backend=0;char name[97];
    memset(c,0,sizeof(*c));
    c->client_empty=1;
    if(!marble_adc_ram_word(base+0xd6b0,&t) || !t)return 0;
    /* Native init 7704: one 130-byte transport; the driver, not us, owns its
     * registered IPCC callback 72e0. +88 poll is absent in this QMP backend. */
    static const unsigned offsets[]={8,0x10,0x18,0x20,0x28,0x30,0x38,0x80};
    static const unsigned rvas[]={0x6ad4,0x6b5c,0x713c,0x6bf4,0x6cac,0x6d50,0x6dd8,0x710c};
    for(unsigned i=0;i<8;i++)if(!marble_adc_ram_word(t+offsets[i],&v) || v!=base+rvas[i])return 0;
    if(!marble_adc_ram_word(t+0xb8,&v) || v!=base+0xbb08 ||
       !marble_adc_ram_word(t+0xc0,&v) || v!=0xc300000 ||
       !marble_adc_ram_word(t+0xc8,&v) || v!=0xc300024 ||
       !marble_adc_ram_word(t+0xd8,&v) || v!=0xc30000c ||
       !marble_adc_ram_word(t+0x88,&v) || v ||
       !marble_adc_ram_word(t+0xa0,&backend) || (backend!=base+0xd3c0 && backend!=base+0xd190) ||
       !marble_adc_ram_word(t+0x98,&core) || !core)return 0;
    if(!marble_adc_ram_word(t+0xe8,&v))return 0;
    c->low_state=(unsigned)(v>>32)&255;
    if(!marble_npa_ram_word(core+0x1c,&v))return 0;
    c->core_state=(uint32_t)v;
    if(!marble_adc_ram_word(core,&v) || !marble_proxy_ram_string(v,name) || !str_eq(name,"qmp") ||
       !marble_adc_ram_word(core+8,&v) || !marble_proxy_ram_string(v,name) || !str_eq(name,"rpm") ||
       !marble_adc_ram_word(core+0x30,&first))return 0;
    uint64_t at=first,seen[8];unsigned count=0;
    while(at && count<8) {
        for(unsigned i=0;i<count;i++)if(seen[i]==at)return 0;
        seen[count++]=at;
        if(!marble_proxy_ram_string(at+8,name) || !str_eq(name,"aop_apps") ||
           !marble_adc_ram_word(at+0x68,&v) || v!=t ||
           !marble_adc_ram_word(at+0x30,&v))return 0;
        c->local=(uint32_t)v;c->remote=(uint32_t)(v>>32);
        if(!marble_adc_ram_word(at+0xd0,&v))return 0;
        c->refs=(uint32_t)v;
        for(unsigned i=0;i<11;i++) {
            if(!marble_adc_ram_word(at+0x70+8*i,&v))return 0;
            if(v)c->client_empty=0;
        }
        if(!marble_adc_ram_word(at,&at))return 0;
    }
    c->count=count;c->transport=t;c->core=core;c->first=first;
    print("marble-qmp-native: context phase=%s transport=%x core=%x low=%d link=%d channels=%d local=%d remote=%d refs=%d terminated=%d poll_present=0 backend_rva=%x client_empty=%d\n",
        phase,t,core,(uint64_t)c->low_state,(uint64_t)c->core_state,(uint64_t)count,
        (uint64_t)c->local,(uint64_t)c->remote,(uint64_t)c->refs,(uint64_t)(!at),backend-base,(uint64_t)c->client_empty);
    return !at && count<=1;
}
static int marble_qmp_native_descriptor(MarbleQmpDescriptor *d)
{
    const volatile uint32_t *p=(const volatile uint32_t *)(uintptr_t)0xc300000;
    for(unsigned i=0;i<sizeof(*d)/4;i++)((uint32_t *)d)[i]=p[i];
    __asm__ volatile("dmb sy" ::: "memory");
    return marble_qmp_valid(d,1024) && !p[d->local_offset/4] && !p[d->remote_offset/4];
}
static unsigned marble_qmp_native_event_calls;
static uint64_t marble_qmp_clock_module;
static int marble_qmp_native_passive_listeners(uint64_t base)
{
    static const efi_guid guid={0x241afae6,0x885f,0x4f6c,{0xa7,0xea,0xc2,0x8e,0xab,0x79,0xc3,0xe5}};
    uint64_t *clock=NULL,clockbase=0,at=0,seen[64];
    if(!EFI_ERROR(BS->locate_protocol(&guid,NULL,(void **)&clock)) && clock) {
        clockbase=(uint64_t)(uintptr_t)clock-0x1b0f8;
        static const unsigned rvas[]={0x14e4,0x1528,0x1570,0x15b8,0x15fc,0x1640,0x1688,0x16cc};
        if(clock[0]!=0x10009)clockbase=0;
        for(unsigned i=0;i<8 && clockbase;i++)if(clock[i+1]!=clockbase+rvas[i])clockbase=0;
    }
    if(!marble_adc_ram_word(base+0xd508,&at))return 0;
    marble_qmp_clock_module=clockbase;
    unsigned count=0,matched=0,unknown=0;
    while(at && count<64) {
        for(unsigned i=0;i<count;i++)if(seen[i]==at)return 0;
        seen[count++]=at;
        uint64_t transport=0,remote=0,callback=0,priv=0;char t[97],r[97];
        if(!marble_adc_ram_word(at+8,&transport) || !marble_adc_ram_word(at+0x10,&remote) ||
           !marble_adc_ram_word(at+0x58,&callback) || !marble_adc_ram_word(at+0x60,&priv))return 0;
        if(transport) {if(!marble_proxy_ram_string(transport,t))return 0;} else memcpy(t,"*",2);
        if(remote) {if(!marble_proxy_ram_string(remote,r))return 0;} else memcpy(r,"*",2);
        unsigned relevant=(!transport || str_eq(t,"qmp")) && (!remote || str_eq(r,"rpm"));
        /* ClockDxe e358 only checks 'rpm' and updates its local link flag.
         * It neither opens a channel nor enables clocks/sends power requests. */
        unsigned passive=clockbase && callback==clockbase+0xe358 && priv==clockbase+0x2ece0;
        if(relevant) {matched++;if(!passive)unknown++;}
        print("marble-qmp-native: listener index=%d transport=%s remote=%s callback=%x clock_rva=%x priv=%x relevant=%d passive=%d\n",(uint64_t)(count-1),t,r,callback,clockbase?callback-clockbase:0,priv,(uint64_t)relevant,(uint64_t)passive);
        if(!marble_adc_ram_word(at,&at))return 0;
    }
    print("marble-qmp-native: listeners PASS=%d total=%d matched=%d unknown=%d terminated=%d callback_calls=0\n",(uint64_t)(!at && !unknown),(uint64_t)count,(uint64_t)matched,(uint64_t)unknown,(uint64_t)(!at));
    return !at && !unknown;
}
static void marble_qmp_native_service(uint64_t base,uint64_t transport)
{
    /* Reuse the exact callback installed by native init at 79bc, without
     * patching its table or reconfiguring IPCC. Exclude IRQ reentry while it
     * holds firmware locks. The provider context and listeners were qualified
     * before the first call. No user-supplied function address is accepted. */
    typedef void (EFIAPI *Event)(void *);
    uint64_t daif=READ_SYSREG(DAIF);
    __asm__ volatile("msr daifset, #0xf" ::: "memory");
    ((Event)(uintptr_t)(base+0x72e0))((void *)(uintptr_t)transport);
    __asm__ volatile("msr daif, %0" :: "r"(daif) : "memory");
    marble_qmp_native_event_calls++;
}
static int marble_qmp_native_load_tx(MarbleGlinkFirmware *provider,uint64_t base,
    const MarbleQmpNativeContext *context,void *handle,unsigned index)
{
    uint64_t word=0;MarbleQmpDescriptor descriptor;
    if(index>1 || marble_qmp_native.tx!=index || marble_qmp_native.tx_pending ||
       marble_qmp_native.invalid || marble_qmp_native.remote_down ||
       !marble_adc_ram_word(context->transport+0xa0,&word) || word!=base+0xd190 ||
       !marble_qmp_native_descriptor(&descriptor) || !marble_qmp_open(&descriptor))return 0;
    static const unsigned offsets[]={0x68,0x70,0x78,0x80,0x88};
    static const unsigned rvas[]={0x38f0,0x3244,0x32c8,0x39bc,0x3948};
    for(unsigned i=0;i<5;i++)if(!marble_adc_ram_word(context->core+offsets[i],&word) || word!=base+rvas[i])return 0;
    if(!marble_adc_ram_word(context->transport+0x60,&word) || word!=base+0x7000 ||
       !marble_adc_ram_word(base+0xd1f0,&word) || word!=base+0x3534)return 0;
    typedef efi_status (EFIAPI *Tx)(void *,const void *,const void *,size_t,uint32_t,int32_t *);
    marble_qmp_native.tx_index=index;marble_qmp_native.tx_pending=1;
    marble_qmp_native.tx_cookie[index]=0x4d514d50u+index;
    uint64_t start=READ_SYSREG(CNTVCT_EL0),freq=READ_SYSREG(CNTFRQ_EL0);
    int32_t result=-999;marble_qmp_native.tx_calls++;
    print("marble-qmp-load: tx_before index=%d value=%s bytes=64 flags=0 DSP_start=0\n",(uint64_t)index,index?"off":"on");
    efi_status status=((Tx)provider->slot[4])(handle,&marble_qmp_native.tx_cookie[index],marble_qmp_load_message[index],64,0,&result);
    print("marble-qmp-load: tx_return index=%d status=%x result=%d\n",(uint64_t)index,status,(uint64_t)(int64_t)result);
    unsigned ok=!EFI_ERROR(status) && !result;
    while(ok && marble_qmp_native.tx_pending && !marble_qmp_native.invalid &&
          !marble_qmp_native.remote_down && (READ_SYSREG(CNTVCT_EL0)-start)*1000000/freq<250000) {
        marble_qmp_native_service(base,context->transport);BS->stall(1000);
    }
    uint64_t elapsed=(READ_SYSREG(CNTVCT_EL0)-start)*1000000/freq;
    ok=ok && !marble_qmp_native.tx_pending && marble_qmp_native.tx==index+1 &&
        !marble_qmp_native.invalid && !marble_qmp_native.rx && !marble_qmp_native.remote_down &&
        elapsed<250000 && marble_qmp_native_descriptor(&descriptor) && marble_qmp_open(&descriptor);
    print("marble-qmp-load: tx_complete index=%d PASS=%d callbacks=%d pending=%d invalid=%d mailbox_empty=%d elapsed_us=%d physical_readback=0\n",
        (uint64_t)index,(uint64_t)ok,(uint64_t)marble_qmp_native.tx,(uint64_t)marble_qmp_native.tx_pending,
        (uint64_t)marble_qmp_native.invalid,(uint64_t)(ok!=0),elapsed);
    return ok;
}
/* features=0 selects Mu's generic core, which requires a remote intent that
 * the QMP mailbox wire protocol cannot advertise. Reuse its exact underlying
 * 7000 transport primitive, with our own static transfer descriptor. Do not
 * fabricate an intent or patch any native function table. Completion below
 * is the Linux QMP empty-mailbox ACK, never a GLink client TX callback. */
typedef struct {
    void *next;const void *data,*packet;
    uint32_t id,pad;uint64_t bytes,remaining;
} MarbleQmpMailboxTransfer;
_Static_assert(offsetof(MarbleQmpMailboxTransfer,id)==0x18,"QMP transfer identifier");
_Static_assert(offsetof(MarbleQmpMailboxTransfer,remaining)==0x28,"QMP transfer bytes");
static MarbleQmpMailboxTransfer marble_qmp_mailbox_transfer;
/* Set only by the owned ROM coordinator while its outer LPI lease is held. */
static unsigned marble_qmp_owned_authorized;
static int marble_qmp_native_queues_empty(void *handle,uint64_t transport)
{
    uint64_t q=0,v=0;
    if(!marble_npa_ram_word((uint64_t)(uintptr_t)handle+0x48,&q) || !q)return 0;
    for(unsigned list=0x28;list<=0x70;list+=0x18)
        for(unsigned off=0;off<0x18;off+=8)
            if(!marble_npa_ram_word(q+list+off,&v) || (off==16?(uint32_t)v:v))return 0;
    if(!marble_npa_ram_word(transport+0x108,&v) || v ||
       !marble_npa_ram_word(transport+0x118,&v) || (uint32_t)v!=0xffffffffu)return 0;
    return 1;
}
static int marble_qmp_native_mailbox_tx(uint64_t base,const MarbleQmpNativeContext *context,void *handle,unsigned index)
{
    uint64_t fn=0,channel=0,mailbox=0;MarbleQmpDescriptor descriptor;
    if(index>1 || marble_qmp_native.tx_acks!=index || marble_qmp_native.tx_pending || marble_qmp_native.tx ||
       marble_qmp_native.invalid || marble_qmp_native.remote_down ||
       !marble_qmp_native_descriptor(&descriptor) || !marble_qmp_open(&descriptor) || descriptor.features ||
       !marble_qmp_native_queues_empty(handle,context->transport) ||
       !marble_adc_ram_word(context->transport+0x60,&fn) || fn!=base+0x7000 ||
       !marble_adc_ram_word(context->transport+0xd0,&mailbox) || mailbox!=0xc300000+descriptor.local_offset ||
       !marble_npa_ram_word((uint64_t)(uintptr_t)handle+0x28,&channel))return 0;
    MarbleQmpMailboxTransfer *transfer=&marble_qmp_mailbox_transfer;
    transfer->data=marble_qmp_load_message[index];transfer->id=0xfffffffeu;transfer->bytes=transfer->remaining=64;
    typedef int32_t (EFIAPI *Send)(void *,unsigned,MarbleQmpMailboxTransfer *);
    marble_qmp_native.tx_pending=1;marble_qmp_native.tx_calls++;
    uint64_t start=READ_SYSREG(CNTVCT_EL0),freq=READ_SYSREG(CNTFRQ_EL0),daif=READ_SYSREG(DAIF);
    print("marble-qmp-load: tx_before index=%d value=%s bytes=64 flags=0 DSP_start=0 adapter=native-mailbox GLink_TX_calls=0\n",(uint64_t)index,index?"off":"on");
    __asm__ volatile("msr daifset, #0xf" ::: "memory");
    int32_t result=((Send)(uintptr_t)fn)((void *)(uintptr_t)context->transport,(unsigned)channel,transfer);
    __asm__ volatile("msr daif, %0" :: "r"(daif) : "memory");
    print("marble-qmp-load: tx_return index=%d status=0 result=%d consumed=%d\n",(uint64_t)index,(uint64_t)(int64_t)result,(uint64_t)(transfer->remaining==0));
    unsigned ok=!result && !transfer->remaining,ack=0;
    while(ok && !marble_qmp_native.invalid && !marble_qmp_native.remote_down &&
          (READ_SYSREG(CNTVCT_EL0)-start)*1000000/freq<250000) {
        marble_qmp_native_service(base,context->transport);
        if(marble_qmp_native_descriptor(&descriptor) && marble_qmp_open(&descriptor) &&
           marble_qmp_native_queues_empty(handle,context->transport)) {ack=1;break;}
        BS->stall(1000);
    }
    uint64_t elapsed=(READ_SYSREG(CNTVCT_EL0)-start)*1000000/freq;
    ok=ok && ack && elapsed<250000 && !marble_qmp_native.tx && !marble_qmp_native.rx && !marble_qmp_native.invalid;
    if(ok) {marble_qmp_native.tx_pending=0;marble_qmp_native.tx_acks++;}
    print("marble-qmp-load: tx_complete index=%d PASS=%d callbacks=%d pending=%d invalid=%d mailbox_empty=%d elapsed_us=%d physical_readback=0 acknowledgements=%d native_queues_empty=%d\n",
        (uint64_t)index,(uint64_t)ok,(uint64_t)marble_qmp_native.tx,(uint64_t)marble_qmp_native.tx_pending,
        (uint64_t)marble_qmp_native.invalid,(uint64_t)ack,elapsed,(uint64_t)marble_qmp_native.tx_acks,(uint64_t)ack);
    return ok;
}
typedef struct {
    MarbleGlinkFirmware *provider;uint64_t base,start,freq,budget;
    MarbleQmpNativeContext before;MarbleQmpDescriptor original;
    void *handle;unsigned pump,peer_close,owned,load_state,valid,paused;
} MarbleQmpSession;
static MarbleQmpSession marble_qmp_session;
static int marble_qmp_native_finish(unsigned ok)
{
    MarbleQmpSession *s=&marble_qmp_session;
    if(!s->valid)return 0;
    MarbleGlinkFirmware *provider=s->provider;
    uint64_t base=s->base,start=s->start,freq=s->freq,budget=s->budget;
    MarbleQmpNativeContext before=s->before,after={0};
    MarbleQmpDescriptor original=s->original,closed={0};
    void *handle=s->handle;
    unsigned pump=s->pump,peer_close=s->peer_close,owned=s->owned,load_state=s->load_state;
    unsigned original_restored=0,peer_closed=0,legacy_pass=0,settled_closed=0;
    typedef efi_status (EFIAPI *Close)(void *,int32_t *);
    efi_status status=0;int32_t result=-999;
    /* Native close asserts for the wrong local state or absent core context.
     * Only call after the exact connected callback and descriptor proof. */
    if(ok) {
        marble_qmp_native.closing=1;result=-999;
        uint64_t close_start=owned?READ_SYSREG(CNTVCT_EL0):start;
        status=((Close)provider->slot[3])(handle,&result);
        print("marble-qmp-native: close_return status=%x result=%d\n",status,(uint64_t)(int64_t)result);
        ok=!EFI_ERROR(status) && !result;
        while(ok && !marble_qmp_native.local_down && !marble_qmp_native.invalid &&
              (READ_SYSREG(CNTVCT_EL0)-close_start)*1000000/freq<1750000) {
            if(pump)marble_qmp_native_service(base,before.transport);
            BS->stall(1000);
        }
        /* The callback may have freed the handle; never dereference or poll it. */
        if(marble_qmp_native.local_down==1)marble_qmp_native.active=0;
        if(peer_close==2 && ok && !marble_qmp_native.active) {
            /* QMP close locally ACKs the remote DOWN while AOP may remain UP.
             * Continue only by the stable transport, never the freed handle.
             * Reconcile the remote-only native node before clearing watchdog. */
            unsigned settled=0,rounds=0;
            while(rounds<32 && (READ_SYSREG(CNTVCT_EL0)-close_start)*1000000/freq<1900000) {
                marble_qmp_native_service(base,before.transport);rounds++;
                BS->stall(1000);
                if(marble_qmp_native_descriptor(&closed) && !memcmp(&original,&closed,sizeof(original))) {settled=1;break;}
            }
            print("marble-qmp-native: settle PASS=%d rounds=%d stale_handle_calls=0 raw_writes=0 TX_calls=%d DSP_start=0\n",(uint64_t)settled,(uint64_t)rounds,(uint64_t)marble_qmp_native.tx_calls);
            ok=ok && settled;
        }
        unsigned descriptor=marble_qmp_native_descriptor(&closed);
        unsigned context=marble_qmp_native_context(base,&after,"after");
        original_restored=descriptor && !memcmp(&original,&closed,sizeof(original));
        /* Native local enum transitions INIT(0)->OPENED(3)->CLOSED(1).
         * The remote-owned reference remains until the remote closes. It is
         * deliberately not called an identical heap/object restoration. */
        unsigned common=ok && !marble_qmp_native.active && !marble_qmp_native.invalid &&
            !marble_qmp_native.rx && !marble_qmp_native.tx && marble_qmp_native.tx_acks==(load_state?2u:0u) && !marble_qmp_native.tx_pending && context &&
            after.low_state==2 && after.core_state==2;
        legacy_pass=common && !marble_qmp_native.remote_down && original_restored &&
            after.count==1 && after.local==1 && after.remote==2 && after.refs==1;
        peer_closed=common && descriptor && marble_qmp_peer_closed(&closed) &&
            marble_qmp_native.remote_down==1 && !after.count;
        settled_closed=common && original_restored && marble_qmp_native.remote_down==1 &&
            after.count==1 && !after.local && after.remote==2 && after.refs==1 && after.client_empty;
        print("marble-qmp-native: close_state descriptor=%d original_restoration=%d legacy_contract_pass=%d peer_closed=%d settled_closed=%d local=%x,%x remote=%x,%x channels=%d native_handles_released=%d local_client_released=%d\n",
            (uint64_t)descriptor,(uint64_t)original_restored,(uint64_t)legacy_pass,(uint64_t)peer_closed,
            (uint64_t)settled_closed,
            (uint64_t)closed.local_channel,(uint64_t)closed.local_channel_ack,(uint64_t)closed.remote_channel,(uint64_t)closed.remote_channel_ack,
            (uint64_t)after.count,(uint64_t)(context && !after.count),(uint64_t)(context && after.client_empty));
        ok=peer_close==2?settled_closed:peer_close?peer_closed:legacy_pass;
    }
    uint64_t elapsed=(READ_SYSREG(CNTVCT_EL0)-start)*1000000/freq;
    ok=ok && elapsed<budget;
    efi_status clear=EFI_ERROR_BIT|7;
    if(ok) {
        if(owned) {clear=~0ULL;marble_qmp_native.dirty=0;}
        else {clear=BS->set_watchdog_timer(0,0,0,NULL);if(!EFI_ERROR(clear))marble_qmp_native.dirty=0;else ok=0;}
    }
    print("marble-qmp-native: complete PASS=%d attempted=1 active=%d dirty=%d connected=%d local_down=%d remote_down=%d invalid=%d RX=%d TX=%d elapsed_us=%d watchdog_clear=%x native_event_calls=%d peer_close_contract=%d settled_close_contract=%d original_restoration=%d raw_writes=0 TX_calls=%d NPA_calls=%s PAS_calls=%s DSP_start=0 storage_access=0 watchdog_retained=%d owned=%d\n",
        (uint64_t)ok,(uint64_t)marble_qmp_native.active,(uint64_t)marble_qmp_native.dirty,
        (uint64_t)marble_qmp_native.connected,(uint64_t)marble_qmp_native.local_down,(uint64_t)marble_qmp_native.remote_down,
        (uint64_t)marble_qmp_native.invalid,(uint64_t)marble_qmp_native.rx,(uint64_t)marble_qmp_native.tx,elapsed,clear,(uint64_t)marble_qmp_native_event_calls,(uint64_t)(peer_close==1),(uint64_t)(peer_close==2),(uint64_t)original_restored,(uint64_t)marble_qmp_native.tx_calls,owned?"coordinated":"0",owned?"coordinated":"0",(uint64_t)owned,(uint64_t)owned);
    return ok;
}
static int marble_qmp_native_cancel_noexec(void)
{
    MarbleQmpSession *s=&marble_qmp_session;
    unsigned clean=s->valid && s->paused && s->owned && s->load_state==4 &&
        marble_qmp_native.active && marble_qmp_native.tx_acks==1 && !marble_qmp_native.tx_pending &&
        !marble_qmp_native.invalid && !marble_qmp_native.remote_down && !marble_pil_auth_attempted &&
        marble_pil_dsp_dirty && marble_pil_deferred.saved && marble_pil_deferred.verified &&
        !marble_pil_ram_device.dirty && marble_pil_ram_device.installed;
    /* Linux pre-auth error unwind removes load_state before metadata cleanup.
     * Never use this cancellation path after auth/reset was even attempted. */
    print("marble-qmp-load: held_return PASS=%d outcome=%d DSP_start=0 ROM_restored=0 watchdog_retained=1 noexec_cancel=1\n",(uint64_t)clean,(uint64_t)(clean?1:0));
    if(!clean)return 0;
    unsigned ok=marble_qmp_native_mailbox_tx(s->base,&s->before,s->handle,1);
    if(ok)s->paused=0;
    return marble_qmp_native_finish(ok);
}
static int marble_qmp_native_cycle(unsigned pump,unsigned peer_close,unsigned load_state)
{
    unsigned owned=load_state==4;
    if(marble_qmp_native.attempted || marble_glink_observer.attempted || marble_cap.active || marble_cap.dirty)return 0;
    if(owned) {
        if(marble_qmp_owned_authorized!=2 || !marble_pil_post_load_hook || !marble_firmware_reset_required ||
           !marble_pil_attempted || !marble_pil_header_only || marble_pil_auth_attempted ||
           !marble_smp2p_owned || !marble_smp2p_notifications || !marble_pil_dsp_dirty ||
           !marble_pil_deferred.saved || !marble_pil_deferred.verified || marble_npa_active!=2 || !marble_npa_dirty)return 0;
    } else if(marble_pil_attempted || marble_smp2p_attempted || marble_npa_attempted ||
              marble_qmp_owned_authorized)return 0;
    uint64_t budget=owned?15000000:2000000;
    if(!marble_qmp_inventory())return 0;
    static const efi_guid guid={0x3bfcf64d,0x00cd,0x41cb,{0xbe,0xa5,0xa7,0x6c,0x3a,0x24,0x3d,0xed}};
    MarbleGlinkFirmware *provider=NULL;
    if(EFI_ERROR(BS->locate_protocol(&guid,NULL,(void **)&provider)) || !provider)return 0;
    uint64_t base=(uint64_t)(uintptr_t)provider-0xd0a8;
    static const unsigned rvas[]={0x144c,0x1484,0x14bc,0x14f4,0x152c,0x1564,0x159c,0x15d4,0x160c,0x1644,0x167c,0x16b4,0x16ec,0x17ac};
    if(provider->revision!=0x10001)return 0;
    for(unsigned i=0;i<14;i++)if((uint64_t)(uintptr_t)provider->slot[i]!=base+rvas[i])return 0;
    if(load_state) {
        if(!pump || peer_close!=2 || (load_state!=2 && !owned))return 0; /* v70 generic TX disabled */
        for(unsigned i=0;i<sizeof(marble_qmp_glink_code)/sizeof(*marble_qmp_glink_code);i++)
            if(!marble_code_digest(marble_npa_ram_word,base+marble_qmp_glink_code[i].rva,
                marble_qmp_glink_code[i].bytes,marble_qmp_glink_code[i].digest))return 0;
        print("marble-qmp-load: qualified code_ranges=13 bytes=64 zero_filled=1 on_then_off=1 DSP_start=0 PAS_calls=0 NPA_calls=0 storage_access=0 adapter=native-mailbox GLink_TX_calls=0 owned=%d deferred=%d\n",(uint64_t)owned,(uint64_t)owned);
    }
    MarbleQmpNativeContext before;MarbleQmpDescriptor original,opened;
    if(!marble_qmp_native_context(base,&before,pump?"link-pending":"before") || !marble_qmp_native_descriptor(&original))return 0;
    if(pump) {
        if(before.low_state!=1 || before.core_state!=1 || before.count ||
           original.local_link!=MARBLE_QMP_UP || original.local_link_ack!=MARBLE_QMP_UP ||
           original.remote_link!=MARBLE_QMP_UP || original.remote_link_ack!=MARBLE_QMP_UP ||
           original.local_channel!=MARBLE_QMP_DOWN || original.local_channel_ack!=MARBLE_QMP_DOWN ||
           original.remote_channel!=MARBLE_QMP_UP || original.remote_channel_ack!=MARBLE_QMP_UP ||
           !marble_qmp_native_passive_listeners(base))return 0;
        MarbleThermalSample sample;
        if(!marble_adc_channels(1) || marble_adc_last_temperature>=38 ||
           !marble_thermal_snapshot(marble_thermal_read,NULL,&sample) || sample.maximum_mc>=50000)return 0;
        if(EFI_ERROR(BS->set_watchdog_timer(30,0,0,NULL)))return 0;
        marble_qmp_native.attempted=1;marble_qmp_native.dirty=1;
        marble_qmp_native_service(base,before.transport);
        unsigned progressed=marble_qmp_native_context(base,&before,"before") && before.low_state==2 && before.core_state==2;
        print("marble-qmp-native: link_progress PASS=%d native_event_calls=%d table_writes=0 raw_writes=0 TX_calls=0 DSP_start=0\n",(uint64_t)progressed,(uint64_t)marble_qmp_native_event_calls);
        if(!progressed || !marble_qmp_native_descriptor(&original))return 0;
        /* Native init 6ad4 acknowledges an already-UP remote channel before
         * core link-up; 7340 later skips its open event because ACK==UP.
         * Reconcile that observed remote-owned channel through the SAME core
         * callback that the native 73bc branch would call (7dc4), only when
         * no channel exists. No remote state or client handle is fabricated. */
        if(!before.count && original.remote_channel==MARBLE_QMP_UP && original.remote_channel_ack==MARBLE_QMP_UP) {
            typedef void (EFIAPI *RemoteOpen)(void *,unsigned,const char *,unsigned);
            uint64_t callback=0;
            if(!marble_adc_ram_word(base+0xd3e0,&callback) || callback!=base+0x7dc4)return 0;
            uint64_t daif=READ_SYSREG(DAIF);
            __asm__ volatile("msr daifset, #0xf" ::: "memory");
            ((RemoteOpen)(uintptr_t)callback)((void *)(uintptr_t)before.transport,0xcd,"aop_apps",0x64);
            __asm__ volatile("msr daif, %0" :: "r"(daif) : "memory");
            unsigned reconciled=marble_qmp_native_context(base,&before,"remote-reconciled") &&
                before.low_state==2 && before.core_state==2 && before.count==1 &&
                !before.local && before.remote==2 && before.refs==1;
            print("marble-qmp-native: remote_reconcile PASS=%d calls=1 observed_remote_up=1 remote_id=cd TX_calls=0 DSP_start=0\n",(uint64_t)reconciled);
            if(!reconciled || !marble_qmp_native_descriptor(&original))return 0;
        }
    }
    unsigned eligible=before.low_state==2 && before.core_state==2 &&
        (!before.count || (before.local==0 && before.remote==2 && before.refs==1)) &&
        original.local_channel==MARBLE_QMP_DOWN && original.local_channel_ack==MARBLE_QMP_DOWN &&
        original.remote_channel==MARBLE_QMP_UP && original.remote_channel_ack==MARBLE_QMP_UP &&
        original.local_link==MARBLE_QMP_UP && original.local_link_ack==MARBLE_QMP_UP &&
        original.remote_link==MARBLE_QMP_UP && original.remote_link_ack==MARBLE_QMP_UP;
    print("marble-qmp-native: preflight qualified=%d exact_abi=1 transport=qmp remote=rpm channel=aop_apps open_attempted=0 firmware_changed=%d TX_calls=0 DSP_start=0\n",(uint64_t)eligible,(uint64_t)marble_qmp_native.attempted);
    if(!eligible)return 0;
    MarbleThermalSample sample;
    if(!marble_adc_channels(1) || marble_adc_last_temperature>=38 ||
       !marble_thermal_snapshot(marble_thermal_read,NULL,&sample) || sample.maximum_mc>=50000)return 0;
    if(EFI_ERROR(BS->set_watchdog_timer(30,0,0,NULL)))return 0;
    marble_qmp_native.attempted=1;marble_qmp_native.dirty=1;
    uint64_t start=READ_SYSREG(CNTVCT_EL0),freq=READ_SYSREG(CNTFRQ_EL0);
    MarbleQmpOpenConfig *cfg=&marble_qmp_native.config;
    cfg->transport="qmp";cfg->remote="rpm";cfg->name="aop_apps";cfg->priv=&marble_qmp_native;
    cfg->rxv=marble_qmp_native_rxv;cfg->tx_done=marble_qmp_native_tx_done;cfg->state=marble_qmp_native_state;cfg->timeout=100;
    if(load_state)cfg->tx_abort=marble_qmp_native_tx_abort;
    typedef efi_status (EFIAPI *Open)(MarbleQmpOpenConfig *,void **,int32_t *);
    void *handle=NULL;int32_t result=-999;
    print("marble-qmp-native: open_before watchdog_seconds=30 limit_us=%d TX_calls=0 DSP_start=0\n",budget);
    efi_status status=((Open)provider->slot[2])(cfg,&handle,&result);
    marble_qmp_native.active=handle!=NULL;
    print("marble-qmp-native: open_return status=%x result=%d handle=%x\n",status,(uint64_t)(int64_t)result,(uint64_t)(uintptr_t)handle);
    unsigned ok=!EFI_ERROR(status) && !result && handle;
    /* The original mode only allows existing IRQ delivery. The explicit
     * qualified-pump mode uses the native event function above; no raw MMIO
     * or table patch substitutes for the absent GlinkRpmRxPoll API. */
    while(ok && !marble_qmp_native.connected && !marble_qmp_native.invalid &&
          (READ_SYSREG(CNTVCT_EL0)-start)*1000000/freq<750000) {
        if(pump)marble_qmp_native_service(base,before.transport);
        BS->stall(1000);
    }
    ok=ok && marble_qmp_native.connected==1 && !marble_qmp_native.remote_down &&
        !marble_qmp_native.invalid && !marble_qmp_native.rx && !marble_qmp_native.tx &&
        marble_qmp_native.observed==handle && marble_qmp_native_descriptor(&opened) && marble_qmp_open(&opened);
    print("marble-qmp-native: connected PASS=%d events=%d invalid=%d RX=%d TX=%d\n",(uint64_t)ok,(uint64_t)marble_qmp_native.connected,(uint64_t)marble_qmp_native.invalid,(uint64_t)marble_qmp_native.rx,(uint64_t)marble_qmp_native.tx);
    marble_qmp_session=(MarbleQmpSession){provider,base,start,freq,budget,before,original,handle,pump,peer_close,owned,load_state,1,0};
    if(ok && load_state) {
        ok=marble_qmp_native_mailbox_tx(base,&before,handle,0);
        if(ok && owned) {
            marble_qmp_session.paused=1;
            print("marble-qmp-load: held_before LPI_clients=2 DSP_start=0 watchdog_retained=1 ROM_loaded=1\n");
            return 1;
        }
        if(ok) {BS->stall(10000);ok=marble_qmp_native_mailbox_tx(base,&before,handle,1);}
    }
    return marble_qmp_native_finish(ok);
}
#endif
#endif
