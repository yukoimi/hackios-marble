/* Mu-marble GlinkDxe observer only. Exact image/ABI/disassembly pinned in
 * out/daily-dsp-v1-plan. Register+deregister a local link notification; never
 * open a channel, TX, queue RX, initialize transport, boot DSP or touch PMIC. */
#ifndef XNULOADER_MARBLE_GLINK_FW_H
#define XNULOADER_MARBLE_GLINK_FW_H
#ifdef XNU_MARBLE
typedef struct {
    const char *transport,*remote;
    uint32_t state;
} MarbleGlinkInfo;
typedef struct {
    uint32_t version,pad;
    const char *transport,*remote;
    void (EFIAPI *notify)(MarbleGlinkInfo *,void *);
    void *handle;
} MarbleGlinkLinkId;
typedef struct { uint64_t revision;void *slot[14]; } MarbleGlinkFirmware;
_Static_assert(sizeof(MarbleGlinkLinkId)==40,"Mu GLink link ID ABI");
_Static_assert(offsetof(MarbleGlinkLinkId,notify)==24,"Mu callback offset");
_Static_assert(offsetof(MarbleGlinkInfo,state)==16,"Mu callback state offset");
static struct {
    unsigned attempted,active,dirty,up,down,invalid;
    MarbleGlinkLinkId id;
} marble_glink_observer;
static void EFIAPI marble_glink_notify(MarbleGlinkInfo *info,void *context)
{
    /* No print/allocation/protocol call from a firmware callback. The exact
     * helper at RVA 0x46cc supplies a stack glink_link_info_type. */
    if(context!=&marble_glink_observer || !info) {marble_glink_observer.invalid++;return;}
    if(info->state==0)marble_glink_observer.up++;
    else if(info->state==1)marble_glink_observer.down++;
    else marble_glink_observer.invalid++;
}
static MarbleGlinkFirmware *marble_glink_provider;
static int marble_glink_begin(void)
{
    static const efi_guid guid={0x3bfcf64d,0x00cd,0x41cb,{0xbe,0xa5,0xa7,0x6c,0x3a,0x24,0x3d,0xed}};
    static const unsigned rvas[14]={0x144c,0x1484,0x14bc,0x14f4,0x152c,0x1564,0x159c,
        0x15d4,0x160c,0x1644,0x167c,0x16b4,0x16ec,0x17ac};
    if(marble_glink_observer.attempted || marble_glink_observer.active || marble_glink_observer.dirty)return 0;
    marble_glink_observer.attempted=1;
    MarbleGlinkFirmware *c=NULL;
    efi_status status=BS->locate_protocol(&guid,NULL,(void **)&c);
    print("marble-glink: locate status=%x present=%d open=0 tx=0 dsp_start=0 pmic_writes=0\n",
          status,(uint64_t)(!EFI_ERROR(status) && c));
    if(EFI_ERROR(status) || !c)return 0;
    uint64_t base=(uint64_t)(uintptr_t)c-0xd0a8;
    if(c->revision!=0x10001)return 0;
    for(unsigned i=0;i<14;i++)if((uint64_t)(uintptr_t)c->slot[i]!=base+rvas[i]) {
        print("marble-glink: exact_abi=0 slot=%d calls=0\n",(uint64_t)i);return 0;
    }
    /* Image 9fbbf72b...: register 0x144c->0x2d30 and deregister
     * 0x1484->0x2e34, no This argument. Remote-name table at 0xd218 calls
     * ADSP 'lpass'; unlike Linux's DT spelling 'adsp'. */
    typedef efi_status (EFIAPI *Register)(MarbleGlinkLinkId *,void *,int32_t *);
    MarbleGlinkLinkId *id=&marble_glink_observer.id;
    id->version=1;id->remote="lpass";id->notify=marble_glink_notify;
    int32_t result=-999;
    marble_glink_observer.active=1;
    status=((Register)c->slot[0])(id,&marble_glink_observer,&result);
    print("marble-glink: register exact_abi=1 remote=lpass status=%x result=%d handle=%x up=%d down=%d invalid=%d\n",
          status,(uint64_t)(int64_t)result,(uint64_t)(uintptr_t)id->handle,
          (uint64_t)marble_glink_observer.up,(uint64_t)marble_glink_observer.down,(uint64_t)marble_glink_observer.invalid);
    int registered=!EFI_ERROR(status) && !result && id->handle;
    marble_glink_provider=c;
    return registered;
}
static int marble_glink_release(unsigned dsp_attempted)
{
    typedef efi_status (EFIAPI *Deregister)(void *,int32_t *);
    MarbleGlinkLinkId *id=&marble_glink_observer.id;
    MarbleGlinkFirmware *c=marble_glink_provider;
    efi_status status=0;int32_t result=-999;
    if(!c)return 0;
    if(id->handle) {
        result=-999;
        status=((Deregister)c->slot[1])(id->handle,&result);
        if(EFI_ERROR(status) || result)marble_glink_observer.dirty=1;
        else id->handle=NULL;
    }
    marble_glink_observer.active=id->handle!=NULL;
    int passed=!marble_glink_observer.active && !marble_glink_observer.dirty && !marble_glink_observer.invalid;
    print("marble-glink: released PASS=%d active=%d dirty=%d up=%d down=%d invalid=%d open=0 tx=0 dsp_start_attempted=%d pmic_writes=0\n",
        (uint64_t)passed,(uint64_t)marble_glink_observer.active,(uint64_t)marble_glink_observer.dirty,
        (uint64_t)marble_glink_observer.up,(uint64_t)marble_glink_observer.down,(uint64_t)marble_glink_observer.invalid,(uint64_t)dsp_attempted);
    return passed;
}
static int marble_glink_probe(void)
{
    int registered=marble_glink_begin();
    int released=marble_glink_release(0);
    return registered && released;
}
#endif
#endif
