/* Marble GT9916S bring-up. No flash commands, firmware upload or block I/O.
 * Register/power definitions: MiCode marble-s-oss 48952ed36228, marble DT.
 * Goodix wire protocol follows bsd/dev/redwood_goodix.c (same repository).
 * Included after the loader's cache and logging helpers.
 */
#ifndef MARBLE_TOUCH_H
#define MARBLE_TOUCH_H
#include "marble_touch_mailbox.h"
#ifndef MT_POLL_TEST
static uint32_t mt_read(uint64_t a) { uint32_t v = *(volatile uint32_t *)(uintptr_t)a; __asm__ volatile("dmb sy" ::: "memory"); return v; }
static void mt_write(uint64_t a, uint32_t v) { *(volatile uint32_t *)(uintptr_t)a = v; __asm__ volatile("dsb sy" ::: "memory"); }
static void mt_delay(unsigned us) {
    uint64_t start = READ_SYSREG(CNTVCT_EL0), ticks = READ_SYSREG(CNTFRQ_EL0) * us / 1000000;
    while (READ_SYSREG(CNTVCT_EL0) - start < ticks) __asm__ volatile("yield");
}
#define MT_GPIO(n) (0x0f100000ULL + 0x1000ULL * (n))
#define MT_TCS (0x17a20000ULL + 0xd00)
static void mt_pin(unsigned n, unsigned output, unsigned pull, unsigned ma) {
    mt_write(MT_GPIO(n), pull | ((ma / 2 - 1) << 6) | (output << 9));
}
static void mt_set(unsigned n, unsigned high) { mt_write(MT_GPIO(n)+4, high ? 2 : 0); }
static unsigned mt_get(unsigned n) { return mt_read(MT_GPIO(n)+4)&1; }

static uint32_t mt_resource(const char *name) {
    const uint8_t *db = (const uint8_t *)(uintptr_t)0x80860000;
    if (db[4]!=0xdb || db[5]!=0x30 || db[6]!=3 || db[7]!=0x0c) return 0;
    char id[8] = {0};
    for (unsigned i=0; i<7 && name[i]; i++) id[i]=name[i];
    for (unsigned s=0; s<8; s++) {
        const uint16_t *slave=(const uint16_t *)(db+8+16*s);
        unsigned offset=slave[1], count=slave[3];
        if (!slave[0] || count>512 || 144+offset+24*count>0x20000) continue;
        for (unsigned i=0; i<count; i++) {
            const uint8_t *e=db+144+offset+24*i;
            unsigned same=1;
            for (unsigned j=0; j<8; j++) if (e[j]!=(uint8_t)id[j]) same=0;
            if (same) return *(const uint32_t *)(e+16);
        }
    }
    return 0;
}

static int mt_supply(const char *name, unsigned mv) {
    uint32_t base=mt_resource(name);
    print("marble-touch: %s resource %x, %d mV\n",name,(uint64_t)base,(uint64_t)mv);
    if (!base || (base>>16&15)!=4) return 0;
    /* Never reuse a busy or enabled TCS. No firmware service is called here. */
    print("marble-touch: TCS status=%x control=%x irq=%x\n",(uint64_t)mt_read(MT_TCS+0x18),(uint64_t)mt_read(MT_TCS+0x14),(uint64_t)mt_read(MT_TCS+4));
    if (!mt_read(MT_TCS+0x18)) return 0;
    uint32_t saved[13];
    unsigned offsets[13]={0,0x10,0x14,0x1c,0x30,0x34,0x38,0x44,0x48,0x4c,0x58,0x5c,0x60};
    for(unsigned i=0;i<13;i++) saved[i]=mt_read(MT_TCS+offsets[i]);
    uint32_t data[3]={mv,1,7};
    mt_write(MT_TCS+0x14,saved[2]&~0x1000000u);
    mt_write(MT_TCS+0x14,saved[2]&~0x1010000u);
    mt_write(MT_TCS+8,1);
    for(unsigned i=0;i<3;i++) {
        mt_write(MT_TCS+0x30+20*i,0x10108);
        mt_write(MT_TCS+0x34+20*i,base+4*i);
        mt_write(MT_TCS+0x38+20*i,data[i]);
    }
    mt_write(MT_TCS+0x10,7); mt_write(MT_TCS+0x1c,7);
    mt_write(MT_TCS,saved[0]|1);
    mt_write(MT_TCS+0x14,0x10000); mt_write(MT_TCS+0x14,0x1010000);
    unsigned i;
    for(i=0;i<2000 && !(mt_read(MT_TCS+4)&1);i++) mt_delay(100);
    if (i==2000) {
        print("marble-touch: RPMh timeout; leaving pending TCS untouched\n");
        return 0;
    }
    mt_write(MT_TCS+0x14,0x10000); mt_write(MT_TCS+0x14,0);
    mt_write(MT_TCS+8,1);
    for(unsigned j=4;j<13;j++) mt_write(MT_TCS+offsets[j],saved[j]);
    mt_write(MT_TCS+0x10,saved[1]); mt_write(MT_TCS+0x1c,saved[3]); mt_write(MT_TCS,saved[0]);
    return 1;
}
static unsigned mt_in=16, mt_out=17;
static uint8_t mt_byte(uint8_t out) {
    unsigned in=0;
    for(int bit=7;bit>=0;bit--) {
        mt_set(mt_out,(out>>bit)&1); mt_delay(1);
        mt_set(18,1); mt_delay(1); in=(in<<1)|mt_get(mt_in); mt_set(18,0);
    }
    return (uint8_t)in;
}
static void mt_reg_read(uint32_t addr, uint8_t *data, unsigned size) {
    mt_set(19,0); mt_byte(0xf1);
    for(int i=24;i>=0;i-=8) mt_byte((uint8_t)(addr>>i));
    mt_byte(0xff); mt_byte(0xff); mt_byte(0xff);
    for(unsigned i=0;i<size;i++) data[i]=mt_byte(0);
    mt_set(19,1); mt_delay(2);
}
static void mt_reg_write(uint32_t addr, const uint8_t *data, unsigned size) {
    mt_set(19,0); mt_byte(0xf0);
    for(int i=24;i>=0;i-=8) mt_byte((uint8_t)(addr>>i));
    for(unsigned i=0;i<size;i++) mt_byte(data[i]);
    mt_set(19,1); mt_delay(2);
}
#endif
static unsigned mt_le16(const uint8_t *p) { return p[0]|p[1]<<8; }
static uint32_t mt_le32(const uint8_t *p) { return p[0]|p[1]<<8|p[2]<<16|(uint32_t)p[3]<<24; }
static int mt_checksum(const uint8_t *p,unsigned n) {
    unsigned sum=0, meaningful=0;
    if(n<3) return 0;
    for(unsigned i=0;i<n-2;i++) { sum+=p[i]; meaningful|=(p[i]!=0 && p[i]!=255); }
    return meaningful && (sum&65535)==mt_le16(p+n-2);
}
static uint32_t mt_report_addr, mt_max_x=1080, mt_max_y=2400;
static unsigned mt_ready, mt_reports, mt_bad;
#ifndef MT_POLL_TEST
static uint32_t mt_saved_cfg[6], mt_saved_io[6];
static void mt_restore_pins(void) {
    for(unsigned n=16;n<=21;n++) { mt_write(MT_GPIO(n)+4,mt_saved_io[n-16]&2); mt_write(MT_GPIO(n),mt_saved_cfg[n-16]); }
}
static int mt_probe(void) {
    uint8_t version[28],info[1024];
    if(mt_ready) return 1;
    for(unsigned n=16;n<=21;n++) {
        mt_saved_cfg[n-16]=mt_read(MT_GPIO(n)); mt_saved_io[n-16]=mt_read(MT_GPIO(n)+4);
        print("marble-touch: gpio%d cfg=%x io=%x\n",(uint64_t)n,(uint64_t)mt_saved_cfg[n-16],(uint64_t)mt_saved_io[n-16]);
    }
    mt_set(20,0); mt_pin(20,1,3,8); mt_pin(21,0,3,8);
    if(!mt_supply("ldoc2",1800)) { mt_restore_pins(); return 0; }
    mt_delay(3000);
    if(!mt_supply("ldoc9",3224)) { mt_restore_pins(); return 0; }
    mt_delay(15000);
    mt_set(19,1); mt_pin(19,1,3,16); mt_set(18,0); mt_pin(18,1,0,6);
    for(unsigned attempt=0;attempt<2;attempt++) {
        /* Change both lines to input before choosing a direction. */
        mt_pin(16,0,0,6); mt_pin(17,0,0,6);
        mt_in=attempt?17:16; mt_out=attempt?16:17;
        mt_set(mt_out,0); mt_pin(mt_out,1,0,6);
        mt_set(20,0); mt_delay(2000); mt_set(20,1); mt_delay(150000);
        mt_reg_read(0x10014,version,28);
        print("marble-touch: MISO gpio%d version",(uint64_t)mt_in);
        for(unsigned i=0;i<28;i++) print(" %x",(uint64_t)version[i]); print("\n");
        if(!mt_checksum(version,28)) continue;
        mt_reg_read(0x10070,info,2);
        unsigned len=mt_le16(info),off=32;
        if(len<82 || len>sizeof(info)) continue;
        mt_reg_read(0x10070,info,len);
        if(!mt_checksum(info,len)) continue;
        for(unsigned i=0;i<5;i++) { if(off>=len) { off=len; break; } off+=1+2*info[off]; }
        if(off+54>len || mt_le16(info+off+48)!=8 || mt_le16(info+off+50)!=8) continue;
        mt_report_addr=mt_le32(info+off+44);
        if(!mt_report_addr || mt_report_addr>0xffffff) continue;
        if(off+144<=len) {
            unsigned x=mt_le16(info+off+138),y=mt_le16(info+off+140);
            if(x>=1080 && x<=20000 && y>=2400 && y<=40000) {mt_max_x=x;mt_max_y=y;}
        }
        mt_ready=1;
        print("marble-touch: READY report=%x range=%dx%d head=%d point=%d\n",(uint64_t)mt_report_addr,(uint64_t)mt_max_x,(uint64_t)mt_max_y,(uint64_t)mt_le16(info+off+48),(uint64_t)mt_le16(info+off+50));
        return 1;
    }
    mt_restore_pins(); print("marble-touch: no verified Goodix response; pins restored\n"); return 0;
}
#endif

static struct mt_mailbox *mt_box;
static struct { unsigned down, x, y; } mt_contacts[10];
static uint64_t mt_last_poll, mt_max_poll_gap;
static unsigned mt_trace_reports;
static unsigned mt_test_down,mt_test_x,mt_test_y;
static struct {
    unsigned active,x0,y0,x1,y1;
    uint64_t began,duration;
} mt_gesture;
static void mt_publish(unsigned type,unsigned id,unsigned x,unsigned y) {
    if(!mt_box) return;
    uint64_t seq=mt_box->produced+1;
    struct mt_event *e=&mt_box->events[(seq-1)%MT_CAPACITY];
    *e=(struct mt_event){seq,READ_SYSREG(CNTVCT_EL0),type,id,x,y};
    clean_dcache((uint64_t)(uintptr_t)e,(uint64_t)(uintptr_t)e+sizeof(*e));
    mt_box->produced=seq;
    clean_dcache((uint64_t)(uintptr_t)mt_box,(uint64_t)(uintptr_t)mt_box+64);
}
static void mt_attach(uint64_t pa,uint64_t size) {
    mt_box=NULL; mt_last_poll=mt_max_poll_gap=0; mt_trace_reports=0;
    mt_test_down=0;
    memset(&mt_gesture,0,sizeof(mt_gesture));
    memset(mt_contacts,0,sizeof(mt_contacts)); mt_reports=mt_bad=0;
    if(size<MT_MAILBOX_OFFSET+MT_MAILBOX_BYTES) return;
    struct mt_mailbox *b=(struct mt_mailbox *)(uintptr_t)(pa+MT_MAILBOX_OFFSET);
    /* Explicit opt-in marker from the host, never overwrite APFS data. */
    if(b->magic!=MT_MAILBOX_MAGIC || (b->version!=1 && b->version!=2) || b->capacity!=MT_CAPACITY || b->event_size!=32) return;
    unsigned version=b->version;
    memset(b,0,MT_MAILBOX_BYTES);
    b->magic=MT_MAILBOX_MAGIC; b->version=version; b->capacity=MT_CAPACITY; b->event_size=32;
    b->ready=mt_ready; mt_box=b;
    clean_dcache((uint64_t)(uintptr_t)b,(uint64_t)(uintptr_t)b+MT_MAILBOX_BYTES);
    print("marble-touch: mailbox pa=%x ready=%d\n",(uint64_t)(uintptr_t)b,(uint64_t)mt_ready);
}
/* Explicit host regression input enters the same RAM mailbox as Goodix
 * samples. This checks the bridge/HID/UI path, not physical sensor response.
 * No firmware command or persistent storage is involved. */
static int mt_test_number(const char **p,unsigned *out) {
    unsigned n=0,digits=0;
    while(**p>='0' && **p<='9') {
        unsigned v=(unsigned)(*(*p)++-'0');
        if(++digits>5 || n>6553 || (n==6553 && v>5))return 0;
        n=n*10+v;
    }
    if(!digits)return 0;*out=n;return 1;
}
static int mt_test_prefix(const char *s,const char *prefix) {
    while(*prefix)if(*s++!=*prefix++)return 0;return 1;
}
static int mt_test_event(const char *command,unsigned busy) {
    if(!mt_box || !mt_ready || !busy)return 0;
    unsigned type=0,x=mt_test_x,y=mt_test_y;
    if(mt_test_prefix(command,"up") && !command[2]) {if(!mt_test_down)return 0;type=3;}
    else {
        if(mt_gesture.active)return 0;
        if(mt_test_prefix(command,"down ")) {if(mt_test_down)return 0;type=1;command+=5;}
        else if(mt_test_prefix(command,"move ")) {if(!mt_test_down)return 0;type=2;command+=5;}
        else return 0;
        if(!mt_test_number(&command,&x) || *command++!=' ' || !mt_test_number(&command,&y) || *command)return 0;
        for(unsigned i=0;i<10;i++)if(mt_contacts[i].down)return 0;
    }
    mt_box->heartbeat++;
    clean_dcache((uint64_t)(uintptr_t)mt_box,(uint64_t)(uintptr_t)mt_box+64);
    mt_publish(type,0,x,y);mt_test_x=x;mt_test_y=y;mt_test_down=type!=3;
    if(type==3)mt_gesture.active=0;
    mt_box->heartbeat++;
    clean_dcache((uint64_t)(uintptr_t)mt_box,(uint64_t)(uintptr_t)mt_box+64);
    return 1;
}
/* One bounded host command starts a device-clock gesture. USB transfers no
 * longer stretch a swipe or tap into a long hold. Physical contacts preempt
 * this regression-only producer; their own release semantics are unchanged. */
static int mt_gesture_start(const char *command,unsigned busy) {
    unsigned v[5];
    if(!mt_box || !mt_ready || !busy || mt_test_down || mt_gesture.active)return 0;
    for(unsigned i=0;i<10;i++)if(mt_contacts[i].down)return 0;
    for(unsigned i=0;i<5;i++) {
        if(!mt_test_number(&command,&v[i]))return 0;
        if(i<4) {if(*command!=' ')return 0;command++;}
        else if(*command)return 0;
    }
    if(v[4]<50 || v[4]>1000)return 0;
    mt_gesture.x0=v[0];mt_gesture.y0=v[1];mt_gesture.x1=v[2];mt_gesture.y1=v[3];
    mt_gesture.began=READ_SYSREG(CNTVCT_EL0);
    mt_gesture.duration=READ_SYSREG(CNTFRQ_EL0)*v[4]/1000;
    mt_box->heartbeat++;
    clean_dcache((uint64_t)(uintptr_t)mt_box,(uint64_t)(uintptr_t)mt_box+64);
    mt_test_down=mt_gesture.active=1;mt_test_x=v[0];mt_test_y=v[1];
    mt_publish(1,0,mt_test_x,mt_test_y);
    mt_box->heartbeat++;
    clean_dcache((uint64_t)(uintptr_t)mt_box,(uint64_t)(uintptr_t)mt_box+64);
    print("marble-touch-gesture: started tick=%d duration_ms=%d x0=%d y0=%d x1=%d y1=%d\n",
        mt_gesture.began,(uint64_t)v[4],(uint64_t)v[0],(uint64_t)v[1],(uint64_t)v[2],(uint64_t)v[3]);
    return 1;
}
/* Called inside the poll's odd heartbeat. Late polls interpolate directly to
 * current time, never replay a burst of stale samples or prolong the hold. */
static void mt_gesture_poll(uint64_t now,unsigned cancel) {
    if(!mt_gesture.active)return;
    uint64_t elapsed=now-mt_gesture.began;
    if(cancel || elapsed>=mt_gesture.duration+READ_SYSREG(CNTFRQ_EL0)*16/1000) {
        if(!cancel && (mt_test_x!=mt_gesture.x1 || mt_test_y!=mt_gesture.y1)) {
            mt_test_x=mt_gesture.x1;mt_test_y=mt_gesture.y1;
            mt_publish(2,0,mt_test_x,mt_test_y);
        }
        mt_publish(3,0,mt_test_x,mt_test_y);
        mt_test_down=mt_gesture.active=0;
        print("marble-touch-gesture: released tick=%d elapsed_us=%d cancelled=%d\n",
            now,elapsed*1000000/READ_SYSREG(CNTFRQ_EL0),(uint64_t)cancel);
        return;
    }
    if(elapsed>mt_gesture.duration)elapsed=mt_gesture.duration;
    unsigned x=(unsigned)((int64_t)mt_gesture.x0+
        ((int64_t)mt_gesture.x1-mt_gesture.x0)*(int64_t)elapsed/(int64_t)mt_gesture.duration);
    unsigned y=(unsigned)((int64_t)mt_gesture.y0+
        ((int64_t)mt_gesture.y1-mt_gesture.y0)*(int64_t)elapsed/(int64_t)mt_gesture.duration);
    if(x!=mt_test_x || y!=mt_test_y)mt_publish(2,0,x,y);
    mt_test_x=x;mt_test_y=y;
}
static void mt_poll(void) {
    if(!mt_box) return;
    uint64_t now=READ_SYSREG(CNTVCT_EL0);
    if(now-mt_last_poll<READ_SYSREG(CNTFRQ_EL0)/120) return;
    if(mt_last_poll && now-mt_last_poll>mt_max_poll_gap) mt_max_poll_gap=now-mt_last_poll;
    mt_last_poll=now;
    /* Odd heartbeat excludes readers until this complete report is published. */
    mt_box->heartbeat++;
    clean_dcache((uint64_t)(uintptr_t)mt_box,(uint64_t)(uintptr_t)mt_box+64);
    mt_box->polls++;
    mt_gesture_poll(now,0);
    /* GPIO IRQ can be a brief notification, not a reliable pending-report flag.
     * A short low pulse can fall between our 120 Hz polls. The data-ready bit
     * in the checksummed report persists until acknowledged, so sample it on
     * every pass. Never invent UP from elapsed time or from the IRQ level. */
    if(mt_ready) {
        uint8_t report[90],zero=0;
        unsigned irq=mt_get(21);
        mt_reg_read(mt_report_addr,report,26);
        /* Retry a torn/failed header before acknowledging any volatile data. */
        if(report[0] && !mt_checksum(report,8)) mt_reg_read(mt_report_addr,report,26);
        if(report[0]) {
            unsigned count=report[2]&15, valid=mt_checksum(report,8);
            if(valid && (report[0]&0x80) && count<=10) {
                if(count>2) mt_reg_read(mt_report_addr+8,report+8,count*8+2);
                if(count && !mt_checksum(report+8,count*8+2)) valid=0;
                unsigned mask=0;
                if(valid) {
                    if(mt_trace_reports++<160) {
                        print("marble-touch: raw tick=%d irq=%d flags=%x count=%d mask=%x gap_us=%d x=%d y=%d\n",
                              now,(uint64_t)irq,(uint64_t)report[0],(uint64_t)count,(uint64_t)mask,
                              mt_max_poll_gap*1000000/READ_SYSREG(CNTFRQ_EL0),
                              (uint64_t)(count?mt_le16(report+10):0),(uint64_t)(count?mt_le16(report+12):0));
                    }
                    /* Validate the complete frame before changing contact state. */
                    for(unsigned i=0;i<count;i++) {
                        const uint8_t *p=report+8+8*i;
                        unsigned id=p[0]>>4,kind=p[0]&15;
                        if(kind==1 || kind==3) { valid=0; break; }
                        if(id>=10 || mask&(1u<<id) || mt_le16(p+2)>mt_max_x || mt_le16(p+4)>mt_max_y) {valid=0;break;}
                        mask|=1u<<id;
                    }
                }
                if(valid) {
                    if(count)mt_gesture_poll(now,1);
                    for(unsigned i=0;i<count;i++) {
                        const uint8_t *p=report+8+8*i; unsigned id=p[0]>>4;
                        unsigned x=mt_le16(p+2)*65535u/mt_max_x,y=mt_le16(p+4)*65535u/mt_max_y;
                        if(!mt_contacts[id].down) mt_publish(1,id,x,y);
                        else if(x!=mt_contacts[id].x || y!=mt_contacts[id].y) mt_publish(2,id,x,y);
                        mt_contacts[id].down=1; mt_contacts[id].x=x; mt_contacts[id].y=y;
                    }
                    for(unsigned id=0;id<10;id++) if(mt_contacts[id].down && !(mask&(1u<<id))) {
                        mt_publish(3,id,mt_contacts[id].x,mt_contacts[id].y); mt_contacts[id].down=0;
                    }
                    mt_reports++;
                }
            } else if(count>10) valid=0;
            if(!valid) mt_bad++;
            /* Acknowledge the volatile report only; no configuration/flash writes. */
            if(valid) mt_reg_write(mt_report_addr,&zero,1);
        }
    }
    mt_box->reports=mt_reports; mt_box->bad=mt_bad;
    mt_box->heartbeat++;
    clean_dcache((uint64_t)(uintptr_t)mt_box,(uint64_t)(uintptr_t)mt_box+64);
}
#endif
