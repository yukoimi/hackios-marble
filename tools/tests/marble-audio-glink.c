#include "marble-audio-smem-fixture.h"
#include "../../xnu/files/bsd/sys/marble_audio_glink.h"
static MarbleAudioGlink g;
static unsigned writes,barriers,kicks,fail_write,fail_kick;
static char trace[100];static unsigned trace_at;
static int write_bytes(void *cookie,uint32_t at,const void *in,uint32_t n)
{
    (void)cookie;assert(at<=sizeof(memory) && n<=sizeof(memory)-at);
    assert((n==4 && (at==0x1cf034 || at==0x1cf038)) || (at>=0x1cf060 && at+n<=0x1d3060));
    writes++;trace[trace_at++]='W';if(writes==fail_write)return 0;
    memcpy(memory+at,in,n);return 1;
}
static void barrier(void *cookie)
{(void)cookie;barriers++;trace[trace_at++]='B';}
static int kick(void *cookie)
{(void)cookie;kicks++;trace[trace_at++]='K';return !fail_kick;}
static void init(void)
{
    fixture(1);memset(&g,0,sizeof(g));writes=barriers=kicks=fail_write=fail_kick=trace_at=0;memset(trace,0,sizeof(trace));
    MarbleAudioGlinkIO io={0,read_bytes,write_bytes,barrier,kick};
    assert(marble_audio_glink_init(&g,&io) && !writes && !barriers && !kicks);
}
static void rx_frame(const uint8_t *frame,unsigned n,unsigned tail)
{
    for(unsigned i=0;i<n;i++)memory[g.memory.item[2]+((tail+i)&16383)]=frame[i];
    g.tail=tail;put32(g.memory.item[0]+8,tail);put32(g.memory.item[0]+12,(tail+n)&16383);
}
int main(void)
{
    init();assert(marble_audio_glink_send(&g,0,1,1,0,0)==1);
    assert(g.head==8 && masmem32(memory+0x1cf034)==8 && !memcmp(trace,"WBWBK",5));
    assert(marble_glink_le32(memory+0x1cf060)==0x10000 && marble_glink_le32(memory+0x1cf064)==1);
    uint8_t name[10]="adsp_apps";
    assert(marble_audio_glink_send(&g,2,1,10,name,10)==1 && g.head==32);
    uint8_t advert[8];mag_put32(advert,512);mag_put32(advert+4,1);
    assert(marble_audio_glink_send(&g,5,1,1,advert,8)==1 && g.head==48);
    /* Wrapped FIFO publication preserves padding and ordering. */
    init();g.head=16376;put32(0x1cf034,g.head);put32(0x1cf030,8192);
    assert(marble_audio_glink_send(&g,2,1,10,name,10)==1 && g.head==16);
    assert(!memcmp(memory+0x1cf060,name,10));for(unsigned i=10;i<16;i++)assert(!memory[0x1cf060+i]);
    assert(writes==3 && barriers==2 && kicks==1);
    /* Empty/full handling never alters indices or grants ownership. */
    init();put32(0x1cf030,16);assert(marble_audio_glink_space(&g,8)==0);
    assert(marble_audio_glink_send(&g,0,1,1,0,0)==0 && !g.failed && !writes);
    uint8_t out[528],frame[48]={0};MarbleGlinkControl message;
    init();assert(!marble_audio_glink_peek(&g,out,sizeof(out),&message));
    mag_put32(frame,9|3u<<16);mag_put32(frame+4,1);mag_put32(frame+8,32);
    for(unsigned i=0;i<32;i++)frame[16+i]=(uint8_t)i;
    rx_frame(frame,48,16376);assert(marble_audio_glink_peek(&g,out,sizeof(out),&message)==1);
    assert(message.command==9 && message.channel==3 && message.bytes==48 && !writes && g.tail==16376);
    MarbleGlinkAssembly assembly={0};assert(marble_glink_fragment(&assembly,message.command,out,message.bytes,message.channel));
    assert(assembly.used==32 && !assembly.active && !memcmp(assembly.data,frame+16,32));
    assert(marble_audio_glink_commit_rx(&g)==1 && g.tail==40 && masmem32(memory+0x1cf038)==40 && writes==1 && !kicks);
    /* Partial RX cannot consume a header; later completion is decoded once. */
    init();rx_frame(frame,48,0);put32(0x1cf03c,8);
    assert(!marble_audio_glink_peek(&g,out,sizeof(out),&message) && !g.pending_rx && !writes);
    put32(0x1cf03c,48);assert(marble_audio_glink_peek(&g,out,sizeof(out),&message)==1);
    assert(marble_audio_glink_commit_rx(&g)==1 && g.rx_messages==1);
    /* Failed writes/kick latch the lease; neither retries nor rollback. */
    for(unsigned stage=1;stage<=3;stage++) {
        init();if(stage<3)fail_write=stage;else fail_kick=1;
        assert(marble_audio_glink_send(&g,0,1,1,0,0)==-1 && g.failed && g.live);
        unsigned saved=writes;assert(marble_audio_glink_send(&g,0,1,1,0,0)==-1 && writes==saved);
        if(stage==3)assert(g.head==8 && g.tx_messages==1);
    }
    init();put32(0x1cf034,8);assert(marble_audio_glink_space(&g,8)==-1 && g.failed && !writes);
    init();rx_frame(frame,48,0);assert(marble_audio_glink_peek(&g,out,sizeof(out),&message)==1);
    assert(marble_audio_glink_peek(&g,out,sizeof(out),&message)==-1 && g.failed && !writes);
    init();assert(!marble_audio_glink_commit_rx(&g) && g.failed && !writes);
    init();mag_put32(advert,1024);assert(marble_audio_glink_send(&g,5,1,1,advert,8)==-1 && !writes);
    init();assert(marble_audio_glink_send(&g,14,2,1,0,0)==-1 && !writes);
    puts("PASS native GLINK rings: exact publication/barriers, TX and RX wrap, backpressure, partial frames, shared decoder, commit-after-dispatch and retained fault ownership; model I/O only");
}
