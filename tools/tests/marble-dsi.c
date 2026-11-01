#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../xnuloader/marble_dsi.h"
typedef struct {uint32_t r[256],response;unsigned writes,elapsed,trigger,fail,busy,blank,fifo_n,panel,level,fail_at;uint32_t fifo[2];} Mock;
static uint32_t rd(void *p,unsigned at) {return ((Mock *)p)->r[at/4];}
static void wr(void *p,unsigned at,uint32_t v) {
    Mock *m=p;m->writes++;
    /* The policy must never touch the video/PHY/reset/power registers. */
    assert(at==0x1d4 || at==0x15c || at==0x17c || at==0x3c || at==0x4c || at==0x90 || at==0x1ec || at==0x110);
    if(at==0x17c) {assert(m->fifo_n<2);m->fifo[m->fifo_n++]=v;return;}
    if(at==0x110) {
        assert(!(v&~(MD_INT_MASKS|MD_OWN_DONE)));
        m->r[at/4]=(m->r[at/4]&~(v&MD_OWN_DONE)&~MD_INT_MASKS)|(v&MD_INT_MASKS);
    } else if(at==0x1d4)m->r[at/4]=0;
    else m->r[at/4]=v;
    if(at==0x15c && v==0x30006)m->fifo_n=0;
    if(at==0x90) {
        m->trigger++;
        if(!m->fail && m->trigger!=m->fail_at) {
            if(m->panel) {
                if(m->fifo[0]==0xc0390003) {
                    assert(m->r[0x4c/4]==8 && (m->fifo[1]&255)==0x51);
                    m->level=((m->fifo[1]>>8)&255)*256+((m->fifo[1]>>16)&255);
                } else {
                    unsigned cmd=m->fifo[0]&255;
                    if(cmd==0x52)m->response=0x22000000|((m->level>>8)<<16)|((m->level&255)<<8);
                    if(cmd==0x0a)m->response=0x219c0000;
                    if(cmd==0xda)m->response=0x21c20000;
                    if(cmd==0xdb)m->response=0x21b60000;
                    if(cmd==0xdc)m->response=0x21320000;
                }
            }
            m->r[0x110/4]|=MD_OWN_DONE;
            m->r[0x1d4/4]=4u<<16;m->r[0x6c/4]=m->response;
        }
        if(m->busy)m->r[8/4]|=1;
    }
}
static void delay(void *p,unsigned us) {((Mock *)p)->elapsed+=us;}
static int blank(void *p) {return ((Mock *)p)->blank;}
static Mock fixture(void) {
    Mock m={0};m.r[0]=0x20060000;m.r[1]=0x1f7;m.r[2]=8;
    m.r[0x3c/4]=0x14000000;m.r[0x4c/4]=4;m.r[0x84/4]=0x80000004;
    m.r[0x110/4]=(1u<<16)|(1u<<25);m.blank=1;m.response=0x219c0000;return m;
}
int main(void) {
    Mock m=fixture();MarbleDsiOps o={&m,rd,wr,delay,blank};MarbleDsiResult r;
    assert(marble_dsi_read_power(&o,&r));assert(r.data[0]==0x9c && r.bytes==1 && r.restored);
    assert(m.trigger==1 && m.fifo_n==2 && m.fifo[0]==0xa006000a && m.fifo[1]==0);
    assert(m.r[0x110/4]==((1u<<16)|(1u<<25))); /* unrelated W1C and mask survive */
    for(unsigned cmd=0;cmd<256;cmd++) {
        m=fixture();
        if(cmd==0x52)m.response=0x22013300;
        int allowed=cmd==0x0a || cmd==0xda || cmd==0xdb || cmd==0xdc || cmd==0x52;
        assert(marble_dsi_read_short(&o,&r,cmd)==allowed);
        if(!allowed)assert(!m.writes && !r.started);
        else assert(m.fifo[0]==(0xa0060000|cmd) && r.restored);
    }
    m=fixture();assert(!marble_dsi_read_short(&o,&r,0x52)); /* truncated brightness */
    assert(r.bytes==1 && r.restored && !r.dirty);
    for(unsigned v=0;v<=5000;v++) {
        unsigned target=marble_dsi_dim_target(v);
        if(target)assert(target>=8 && target<v && v-target>=16 && v<=4095 && ((target>327)==(v>327)));
        m=fixture();int allowed=v>=8 && v<=4095;
        assert(marble_dsi_write_brightness(&o,&r,v)==allowed);
        if(!allowed)assert(!m.writes);
        else assert(m.fifo[0]==0xc0390003 && m.fifo[1]==(0xff000051|((v>>8)<<8)|((v&255)<<16)) && r.restored);
    }
    m=fixture();m.panel=1;m.level=614;
    assert(marble_dsi_write_brightness(&o,&r,460));assert(m.level==460);
    assert(marble_dsi_read_short(&o,&r,0x52));assert(r.bytes==2 && r.data[0]==1 && r.data[1]==204);
    assert(marble_dsi_write_brightness(&o,&r,614));assert(m.level==614);
    assert(marble_dsi_read_short(&o,&r,0x52));assert(r.data[0]==2 && r.data[1]==102);
    m=fixture();m.panel=1;m.level=614;MarbleBrightnessLease lease={0};
    assert(marble_brightness_prepare(&o,&lease));assert(lease.original==614 && !lease.active);
    unsigned before=m.writes;
    assert(!marble_brightness_set(&o,&lease,327));assert(!marble_brightness_set(&o,&lease,615));
    assert(m.writes==before && !lease.active);
    assert(marble_brightness_set(&o,&lease,460));assert(lease.active && m.level==460);
    assert(marble_brightness_restore(&o,&lease));assert(!lease.active && !lease.dirty && m.level==614);
    m=fixture();m.panel=1;m.level=614;lease=(MarbleBrightnessLease){0};
    assert(marble_brightness_prepare(&o,&lease));m.fail_at=m.trigger+1;
    assert(!marble_brightness_set(&o,&lease,460));assert(lease.active && !lease.dirty);
    assert(marble_brightness_restore(&o,&lease));assert(!lease.active && m.level==614);
    assert(marble_brightness_set(&o,&lease,460));m.busy=1;
    assert(!marble_brightness_restore(&o,&lease));assert(lease.active && lease.dirty);
    before=m.writes;assert(!marble_brightness_restore(&o,&lease));assert(m.writes==before);
    for(unsigned i=0;i<5;i++) {
        m=fixture();if(i==0)m.r[0]=0;if(i==1)m.r[0x15c/4]=1;
        if(i==2)m.r[0x110/4]|=1;if(i==3)m.r[0x68/4]=1;if(i==4)m.blank=0;
        assert(!marble_dsi_read_power(&o,&r));assert(!m.writes && !r.started);
    }
    m=fixture();m.fail=1;assert(!marble_dsi_read_power(&o,&r));
    assert(r.restored && !r.dirty && m.elapsed>=5000 && m.elapsed<=5010);
    m=fixture();m.fail=1;m.busy=1;assert(!marble_dsi_read_power(&o,&r));
    assert(r.dirty && !r.restored && m.r[0x15c/4]==0x30006);
    r=(MarbleDsiResult){.read_count=4,.response=0x22123400};
    assert(marble_dsi_parse_short(&r) && r.bytes==2 && r.data[0]==0x12 && r.data[1]==0x34);
    r.read_count=8;assert(!marble_dsi_parse_short(&r));
    r.read_count=4;r.response=0x02010000;assert(!marble_dsi_parse_short(&r));
    puts("marble DSI: reject busy/mismatch; bounded completion; packet bytes; W1C isolation; balanced configuration PASS");
}
