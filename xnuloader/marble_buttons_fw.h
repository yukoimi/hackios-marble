/* marble bus 0, PMIC arbiter v7. Android DT and the matching MiCode driver
 * are retained in out/daily-v1-plan. Only observer EXT_READL commands are
 * issued; there is no PMIC write, IRQ setup, pin configuration or reset. */
#ifndef XNULOADER_MARBLE_BUTTONS_FW_H
#define XNULOADER_MARBLE_BUTTONS_FW_H
#include "marble_button_debounce.h"
static unsigned marble_button_channels[2],marble_buttons_ready;
static int marble_button_read(unsigned which,unsigned reg,uint8_t *out,unsigned count)
{
    if(which>1 || !count || count>8 || reg>255 || reg+count>256)return 0;
    uint64_t channel=0xc440000ULL+0x20*marble_button_channels[which];
    uint64_t (EFIAPI *raise)(uint64_t)=(void *)BS->raise_tpl;
    void (EFIAPI *restore)(uint64_t)=(void *)BS->restore_tpl;
    uint64_t tpl=raise(16),start=READ_SYSREG(CNTVCT_EL0);
    mt_write(channel,(1u<<27)|(reg<<4)|(count-1));
    uint32_t status;
    do { status=mt_read(channel+8); }
    while(!(status&1) && READ_SYSREG(CNTVCT_EL0)-start<READ_SYSREG(CNTFRQ_EL0)/1000);
    uint32_t low=mt_read(channel+0x18),high=count>4?mt_read(channel+0x1c):0;
    restore(tpl);
    if((status&15)!=1) {
        print("marble-buttons: read failed which=%d reg=%x status=%x\n",(uint64_t)which,(uint64_t)reg,(uint64_t)status);
        return 0;
    }
    for(unsigned i=0;i<count;i++)out[i]=(uint8_t)((i<4?low:high)>>(8*(i%4)));
    return 1;
}
static int marble_buttons_probe(void)
{
    marble_buttons_ready=0;
    static const uint64_t pages[]={0xc400000,0xc402000,0xc42d000,0xc440000,0xc447000};
    for(unsigned i=0;i<sizeof(pages)/sizeof(pages[0]);i++) {
        uint64_t par=marble_fw_translation(pages[i]);
        if((par&1) || (par&0x0000fffffffff000ULL)!=pages[i]) {
            print("marble-buttons: mapping unqualified address=%x par=%x; no register reads\n",pages[i],par);return 0;
        }
    }
    uint32_t version=mt_read(0xc400000),count=mt_read(0xc400004)&0x7ff;
    print("marble-buttons: observer-probe-v1 version=%x channels=%d pmic_writes=0\n",(uint64_t)version,(uint64_t)count);
    if(version<0x70000000 || version>=0x80000000 || !count || count>1024)return 0;
    static const unsigned ppid[]={0x013,0x18d};
    for(unsigned target=0;target<2;target++) {
        unsigned found=0;
        for(unsigned n=0;n<count;n++) {
            uint32_t map=mt_read(0xc402000+4*n);
            if(!map || ((map>>8)&0xfff)!=ppid[target])continue;
            unsigned owner=mt_read(0xc42d000+4*n)&7;
            if(!found || !owner) {marble_button_channels[target]=n;found=1;}
            print("marble-buttons: ppid=%x channel=%d owner=%d\n",(uint64_t)ppid[target],(uint64_t)n,(uint64_t)owner);
        }
        if(!found)return 0;
    }
    uint8_t pon[2],pin[2],ps=0,gs=0,mode=0,pull=0,enable=0;
    if(!marble_button_read(0,4,pon,2) || !marble_button_read(1,4,pin,2) ||
       !marble_button_read(0,0x10,&ps,1) || !marble_button_read(1,0x10,&gs,1) ||
       !marble_button_read(1,0x40,&mode,1) || !marble_button_read(1,0x42,&pull,1) ||
       !marble_button_read(1,0x46,&enable,1))return 0;
    unsigned qualified=pon[0]==1 && pon[1]==9 && pin[0]==0x10 && pin[1]>=0x10 && pin[1]<=0x13 &&
        !(mode&3) && (enable&0x80) && (pull&7)<=3;
    print("marble-buttons: pon_type=%x subtype=%x gpio_type=%x subtype=%x mode=%x pull=%x enable=%x pon_status=%x gpio_status=%x qualified=%d\n",
        (uint64_t)pon[0],(uint64_t)pon[1],(uint64_t)pin[0],(uint64_t)pin[1],(uint64_t)mode,(uint64_t)pull,
        (uint64_t)enable,(uint64_t)ps,(uint64_t)gs,(uint64_t)qualified);
    marble_buttons_ready=qualified;
    print("marble-buttons: probe PASS=%d pmic_writes=0 observer_read_commands=7 physical_edges_tested=0\n",(uint64_t)qualified);
    return qualified;
}
#ifndef MBUTTONS_TEST
static MarbleButton marble_keys[3];
static uint64_t marble_keys_last,marble_keys_test_at[3];
static unsigned marble_keys_test[3],marble_keys_polls,marble_keys_failed,marble_keys_events;
static void marble_buttons_attach(void)
{
    marble_buttons_ready=0;marble_keys_last=0;
    marble_keys_polls=marble_keys_failed=marble_keys_events=0;
    memset(marble_keys,0,sizeof(marble_keys));memset(marble_keys_test,0,sizeof(marble_keys_test));
    if(mt_box && mt_box->version==2)marble_buttons_probe();
}
static void marble_buttons_poll(void)
{
    if(!marble_buttons_ready || !mt_box || mt_box->version!=2)return;
    uint64_t now=READ_SYSREG(CNTVCT_EL0),frequency=READ_SYSREG(CNTFRQ_EL0);
    if(now-marble_keys_last<frequency/60)return;
    marble_keys_last=now;marble_keys_polls++;
    uint8_t pon=0,gpio=0;
    unsigned valid=marble_button_read(0,0x10,&pon,1) && marble_button_read(1,0x10,&gpio,1);
    if(!valid)marble_keys_failed++;
    unsigned held[3]={!!(pon&0x80),!(gpio&1),!!(pon&0x40)};
    mt_box->heartbeat++;
    clean_dcache((uint64_t)(uintptr_t)mt_box,(uint64_t)(uintptr_t)mt_box+64);
    for(unsigned i=0;i<3;i++) {
        if(marble_keys_test[i]) {
            if(now-marble_keys_test_at[i]<frequency*2)continue;
            mt_publish(5,i+1,0,0);marble_keys_test[i]=0;
            print("marble-buttons: synthetic release timeout key=%d\n",(uint64_t)(i+1));
        }
        unsigned event=0;
        if(valid)event=marble_button_update(&marble_keys[i],held[i]);
        else {if(marble_keys[i].down)event=5;memset(&marble_keys[i],0,sizeof(marble_keys[i]));}
        if(event) {mt_publish(event,i+1,0,0);marble_keys_events++;}
    }
    mt_box->heartbeat++;
    clean_dcache((uint64_t)(uintptr_t)mt_box,(uint64_t)(uintptr_t)mt_box+64);
    if(marble_keys_failed>=3)marble_buttons_ready=0;
    if(marble_keys_polls%300==0 || !valid)
        print("marble-buttons: runtime ready=%d polls=%d events=%d failed=%d pmic_writes=0\n",
            (uint64_t)marble_buttons_ready,(uint64_t)marble_keys_polls,(uint64_t)marble_keys_events,(uint64_t)marble_keys_failed);
}
static int marble_button_test(const char *command,unsigned busy)
{
    if(!busy || !mt_box || mt_box->version!=2 || !marble_buttons_ready)return 0;
    unsigned type=0,id=0;
    if(mt_test_prefix(command,"down ")) {type=4;command+=5;}
    else if(mt_test_prefix(command,"up ")) {type=5;command+=3;}
    if(!type || !mt_test_number(&command,&id) || *command || id<1 || id>3)return 0;
    if(marble_keys[id-1].down || (type==4)==!!marble_keys_test[id-1])return 0;
    marble_keys_test[id-1]=type==4;marble_keys_test_at[id-1]=READ_SYSREG(CNTVCT_EL0);
    mt_box->heartbeat++;clean_dcache((uint64_t)(uintptr_t)mt_box,(uint64_t)(uintptr_t)mt_box+64);
    mt_publish(type,id,0,0);
    mt_box->heartbeat++;clean_dcache((uint64_t)(uintptr_t)mt_box,(uint64_t)(uintptr_t)mt_box+64);
    print("marble-buttons: synthetic type=%d key=%d\n",(uint64_t)type,(uint64_t)id);
    return 1;
}
#endif
#endif
