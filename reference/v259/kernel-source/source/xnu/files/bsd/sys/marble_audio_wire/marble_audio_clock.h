/* Fixed board PRM clock lease. Exact MiCode audio_prm.c plus ROM .ko layout:
 * request76/release64, PRM2/source7, PARAM_RSC_AUDIO_HW_CLK0800102c.
 * Tertiary204: 96kHz*4*32=12.288MHz, inverted uncoupled attr4/root0.
 * This does not configure pins, graphs, amplifiers or sample buffers. */
#ifndef MARBLE_AUDIO_CLOCK_H
#define MARBLE_AUDIO_CLOCK_H
#include "marble_gpr.h"
#define MARBLE_AUDIO_CLOCK_REQUEST_TOKEN 0x4d434b01u
#define MARBLE_AUDIO_CLOCK_RELEASE_TOKEN 0x4d434b02u
typedef struct {uint32_t kind,error,client_data;} MarbleAudioClockReply;
static unsigned marble_audio_clock_command(uint8_t *out,unsigned capacity,unsigned release)
{
    unsigned n=release?64:76;
    if(!out || release>1 || capacity<n)return 0;
    for(unsigned i=0;i<n;i++)out[i]=0;
    marble_gpr_put32(out,n<<8|0x60);marble_gpr_put32(out+4,0x302);
    marble_gpr_put32(out+8,7);marble_gpr_put32(out+12,2);
    marble_gpr_put32(out+16,release?MARBLE_AUDIO_CLOCK_RELEASE_TOKEN:MARBLE_AUDIO_CLOCK_REQUEST_TOKEN);
    marble_gpr_put32(out+20,release?0x01001010:0x0100100f);
    marble_gpr_put32(out+36,n-40);marble_gpr_put32(out+40,2);
    marble_gpr_put32(out+44,0x0800102c);marble_gpr_put32(out+48,n-56);
    marble_gpr_put32(out+56,1);marble_gpr_put32(out+60,0x204);
    if(!release){marble_gpr_put32(out+64,12288000);marble_gpr_put32(out+68,4);}
    return n;
}
static int marble_audio_clock_reply(const uint8_t *data,unsigned bytes,unsigned release,MarbleAudioClockReply *out)
{
    if(!data || !out || bytes!=32 || release>1 || marble_gpr_u32(data)!=0x2060 ||
       (marble_gpr_u32(data+4)&0xff00ffffu)!=0x203 || marble_gpr_u32(data+8)!=2 || marble_gpr_u32(data+12)!=7 ||
       marble_gpr_u32(data+16)!=(release?MARBLE_AUDIO_CLOCK_RELEASE_TOKEN:MARBLE_AUDIO_CLOCK_REQUEST_TOKEN))return 0;
    uint32_t opcode=marble_gpr_u32(data+20),subject=marble_gpr_u32(data+24);MarbleAudioClockReply r={0};
    if(opcode==(release?0x02001003u:0x02001002u) && subject==0x0800102c)r.kind=1;
    else if(opcode==MARBLE_GPR_BASIC_REPLY && subject==(release?0x01001010u:0x0100100fu))r.kind=2;
    else return 0;
    r.error=marble_gpr_u32(data+28);r.client_data=data[6];
    if(r.kind==2 && !r.error)return 0; /* only typed PRM result can grant/release */
    *out=r;return 1;
}
/* Read-only GPIO boundary: resets/speaker switch low and every TDM pin in
 * GPIO input mode. No guessed pin mux, PMIC write, amplifier output or data. */
static const uint8_t marble_audio_clock_guard_pins[]={1,120,52,121,123,122,124};
static int marble_audio_clock_pin_valid(unsigned i,uint32_t cfg,uint32_t io)
{
    if(i>=7)return 0;
    if(i<3)return cfg==1 && !(io&3);
    return !(cfg&0x23c); /* mux bits[5:2]=0, output enable bit9=0 */
}
#endif
