#include "marble-audio-smem-fixture.h"
#include "../../xnu/files/bsd/sys/marble_audio_endpoint.h"
static MarbleAudioEndpoint endpoint;
static MarbleAudioQueue queue;
static _Alignas(4096) uint8_t page[4096];
static unsigned periods,published,write_count,kick_count,fw_advertised,fw_busy,bad_token,limit;
static unsigned defer_refill;
static int io_write(void *cookie,uint32_t at,const void *in,uint32_t n)
{
    (void)cookie;assert((n==4 && (at==0x1cf034 || at==0x1cf038)) || (at>=0x1cf060 && at+n<=0x1d3060));
    memcpy(memory+at,in,n);write_count++;return 1;
}
static void io_barrier(void *cookie){(void)cookie;}
static int io_kick(void *cookie){(void)cookie;kick_count++;return 1;}
static int publish(void *cookie,const void *data,unsigned n)
{
 (void)cookie;assert(data==page && n==4096 && endpoint.stream.start_retired==1);
 MarbleAudioProof proof;
 if(!published) {
  assert(marble_audio_stream_export_proof(&endpoint.stream,MAPROOF_CONFIGURE,&proof));
  assert(marble_audio_proof_valid(&proof,0x1234567800000001ULL,MAPROOF_CONFIGURE));
  MarbleAudioProof bad=proof;bad.flags&=~MAPROOF_START_TX;
  assert(!marble_audio_proof_valid(&bad,proof.epoch,MAPROOF_CONFIGURE));
  bad=proof;bad.unmap[0]=1;assert(!marble_audio_proof_valid(&bad,proof.epoch,MAPROOF_CONFIGURE));
 } else assert(!marble_audio_stream_export_proof(&endpoint.stream,MAPROOF_CONFIGURE,&proof));
 assert(!marble_audio_stream_export_proof(&endpoint.stream,MAPROOF_RETIRED,&proof));
 published++;return 1;
}
static void pattern(unsigned index)
{
    int16_t frames[512];for(unsigned i=0;i<512;i++)frames[i]=(int16_t)(index*257+i);
    assert(marble_audio_queue_push(&queue,frames,256)==256);
}
static void rx(const uint8_t *frame,unsigned n)
{
    uint32_t head=masmem32(memory+0x1cf03c),tail=masmem32(memory+0x1cf038),available;
    assert(marble_glink_ring(16384,head,tail,&available) && n+16<=16384-available);
    for(unsigned i=0;i<n;i++)memory[0x1d3070+((head+i)&16383)]=frame[i];
    put32(0x1cf03c,(head+n)&16383);
}
static void control(unsigned command,unsigned channel,unsigned parameter,const uint8_t *data,unsigned n)
{
    uint8_t frame[64]={0};assert(n<=56);mag_put32(frame,command|channel<<16);mag_put32(frame+4,parameter);
    if(n)memcpy(frame+8,data,n);rx(frame,(8+n+7)&~7u);
}
static void data_fragment(unsigned command,const uint8_t *data,unsigned n,unsigned left)
{
    uint8_t frame[80]={0};assert(n<=64);mag_put32(frame,command|3u<<16);mag_put32(frame+4,1);
    mag_put32(frame+8,n);mag_put32(frame+12,left);memcpy(frame+16,data,n);rx(frame,(16+n+7)&~7u);
}
static void gpr_response(const uint8_t *request,unsigned n,unsigned intent)
{
    assert(fw_advertised && !fw_busy);fw_busy=1;
    uint32_t opcode=masmem32(request+20),token=masmem32(request+16);
    uint8_t r[56]={0};unsigned bytes=32;
    if(opcode==0x0100100c)bytes=28;
    if(opcode==0x0400100a)bytes=56;
    mag_put32(r,bytes<<8|0x60);mag_put32(r+4,0x203);mag_put32(r+8,opcode==0x0400100a?0x6002:1);
    mag_put32(r+12,3);mag_put32(r+16,token);mag_put32(r+20,MARBLE_GPR_BASIC_REPLY);mag_put32(r+24,opcode);
    if(opcode==0x0100100c) {assert(n==44);mag_put32(r+20,0x02001001);mag_put32(r+24,0x80001234);}
    else if(opcode==0x01001000) {
        assert(n==608 && masmem32(request+556)==48000 && masmem32(request+592)==96000);
    } else if(opcode==0x0400100a) {
        assert(n==68 && masmem32(request+32)==0x80001234 && masmem32(request+36)==1024);
        for(unsigned i=0;i<512;i++) {
            uint16_t sample=(uint16_t)page[2*i]|(uint16_t)page[2*i+1]<<8;
            assert(sample==(periods==2?0:(uint16_t)(periods*257+i)));
        }
        mag_put32(r+20,0x05001004);mag_put32(r+24,0x10000000);mag_put32(r+28,1);mag_put32(r+32,0x80001234);
        if(bad_token)r[16]^=1;
        periods++;
        if(periods<limit && periods!=2 && !(defer_refill && periods==3))pattern(periods);
    } else assert(opcode==0x01001001 || opcode==0x01001002 || opcode==0x01001003 || opcode==0x01001004 || opcode==0x0100100d);
    if(periods&1)control(14,3,intent,0,0);
    if(bytes==56) {data_fragment(9,r,32,24);data_fragment(12,r+32,24,0);}
    else data_fragment(9,r,bytes,0);
    if(!(periods&1))control(14,3,intent,0,0);
}
static void firmware(void)
{
    for(unsigned guard=0;guard<64;guard++) {
        uint32_t tail=masmem32(memory+0x1cf030),head=masmem32(memory+0x1cf034),available;
        assert(marble_glink_ring(16384,head,tail,&available));if(!available)return;
        uint8_t frame[1040]={0};for(unsigned i=0;i<16 && i<available;i++)frame[i]=memory[0x1cf060+((tail+i)&16383)];
        unsigned cmd=masmem16(frame),channel=masmem16(frame+2),param=masmem32(frame+4),n=8;
        if(cmd==2)n=(8+param+7)&~7u;
        if(cmd==5)n=16;
        if(cmd==9)n=(16+masmem32(frame+8)+7)&~7u;
        assert(n<=available && n<=sizeof(frame) && !(n&7));
        for(unsigned i=0;i<n;i++)frame[i]=memory[0x1cf060+((tail+i)&16383)];
        if(cmd==0) {
            assert(channel==1 && param==1);control(0,1,1,0,0);control(1,1,1,0,0);
            control(2,3,10,(const uint8_t *)"adsp_apps",10);
        } else if(cmd==2) {
            assert(channel==1 && param==10 && !memcmp(frame+8,"adsp_apps",10));
            control(4,1,0,0,0);uint8_t intents[16];
            mag_put32(intents,128);mag_put32(intents+4,7);mag_put32(intents+8,1024);mag_put32(intents+12,9);
            control(5,3,2,intents,16);
        } else if(cmd==5) {assert(channel==1 && param==1 && masmem32(frame+8)==512 && masmem32(frame+12)==1);fw_advertised=1;}
        else if(cmd==9)gpr_response(frame+16,masmem32(frame+8),param);
        else if(cmd==14) {assert(channel==1 && param==1 && fw_busy);fw_busy=0;}
        else assert(cmd==1 || cmd==4);
        put32(0x1cf030,(tail+n)&16383);
    }
    assert(!"unbounded firmware work");
}
static void init(unsigned count,unsigned corrupt)
{
    fixture(1);memset(&endpoint,0,sizeof(endpoint));memset(&queue,0,sizeof(queue));
    periods=published=write_count=kick_count=fw_advertised=fw_busy=0;bad_token=corrupt;limit=count;
    defer_refill=0;
    pattern(0);MarbleAudioGlinkIO io={0,read_bytes,io_write,io_barrier,io_kick};
    assert(marble_audio_endpoint_init(&endpoint,&io,publish,&queue,page,count,0));
    assert(marble_audio_stream_bind_epoch(&endpoint.stream,0x1234567800000001ULL));
}
int main(void)
{
    init(2048,0);int result=0;
    for(unsigned tick=0;tick<10000 && result==0;tick++) {result=marble_audio_endpoint_poll(&endpoint,tick*100);firmware();}
    if(result!=1)fprintf(stderr,"endpoint debug result=%d phase=%u pending=%u tx=%u reply=%u failed=%u ring_failed=%u controls=%u periods=%u advertised=%u ready=%u\n",result,endpoint.stream.phase,endpoint.stream.pending,endpoint.stream.tx,endpoint.stream.reply,endpoint.failed,endpoint.ring.failed,endpoint.control_head-endpoint.control_tail,periods,endpoint.advertised,endpoint.ready);
    assert(result==1 && periods==2048 && published==2048 && !fw_busy);
    assert(endpoint.stream.completed==2048 && endpoint.ready && !endpoint.failed && !endpoint.ring.failed);
    assert(queue.consumed==2047u*256 && queue.silence==256 && marble_audio_stream_protocol_retired(&endpoint.stream));
    assert(marble_audio_endpoint_poll(&endpoint,9000000)==1 && !endpoint.failed);
    unsigned drained=endpoint.ring.rx_messages;
    control(13,0,0,0,0);
    assert(marble_audio_endpoint_poll(&endpoint,10000000)==1 && endpoint.ring.rx_messages==drained+1);
    assert(!marble_audio_stream_reset_after_pas(&endpoint.stream,0,1));
    /* Actual malformed GPR through the framing/fragment path retires the
     * RX intent once, then stops submission while retaining DMA state. */
    init(8,1);result=0;
    for(unsigned tick=0;tick<100 && !result;tick++) {result=marble_audio_endpoint_poll(&endpoint,tick*100);firmware();}
    assert(result==-1 && endpoint.failed && endpoint.stream.failed && endpoint.stream.live);
    assert(periods==1 && !fw_busy && !marble_audio_stream_protocol_retired(&endpoint.stream));
    unsigned count=write_count;assert(marble_audio_endpoint_poll(&endpoint,100000)==-1 && write_count==count);
    puts("PASS native endpoint model: VERSION/OPEN/INTENT, exact MFC graph, 2048 real-framed GPR transactions, alternating TX/DONE, fragments, slot reuse, stereo+underrun, malformed reply retirement and retained DMA; no hardware provider/playback acceptance");
    return 0;
}
