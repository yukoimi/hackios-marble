/* Runtime FDT tert_tdm_pinctrl + pinned cape PINGROUP121..124: function1,
 * drive8mA, clock/WS output-high, data0 bias-disable, data1 pull-down. */
#ifndef MARBLE_AUDIO_PINS_H
#define MARBLE_AUDIO_PINS_H
#include <stdint.h>
static const uint32_t marble_audio_pins_cfg[]={0x2c4,0x2c4,0xc4,0xc5};
typedef struct {
    void *cookie;int (*read)(void *,unsigned,uint32_t *,uint32_t *);
    int (*write)(void *,unsigned,unsigned,uint32_t);
} MarbleAudioPinOps;
typedef struct {uint32_t cfg[7],io[7];unsigned active,started,restored,cfg_mask,io_mask,writes,failed;} MarbleAudioPins;
static int marble_audio_pins_amps(const MarbleAudioPinOps *o)
{
    uint32_t c,v;for(unsigned i=0;i<3;i++)if(!o->read(o->cookie,i,&c,&v) || c!=1 || (v&3))return 0;
    return 1;
}
static int marble_audio_pins_write(const MarbleAudioPinOps *o,MarbleAudioPins *l,unsigned i,unsigned io,uint32_t v)
{
    if(i<3 || i>=7 || io>1 || (io && (v&~2u)))return 0;
    if(io)l->io_mask|=1u<<i;else l->cfg_mask|=1u<<i;
    l->writes++;return o->write(o->cookie,i,io,v);
}
static int marble_audio_pins_begin(const MarbleAudioPinOps *o,MarbleAudioPins *l)
{
    if(!o || !l || !o->read || !o->write || l->started || l->active)return 0;
    uint32_t c,v;
    for(unsigned i=0;i<7;i++) {
        if(!o->read(o->cookie,i,&l->cfg[i],&l->io[i]) || l->cfg[i]!=1 || (l->io[i]&2) || (i<3 && (l->io[i]&3)))return 0;
    }
    for(unsigned i=0;i<7;i++)if(!o->read(o->cookie,i,&c,&v) || c!=l->cfg[i] || ((v^l->io[i])&2))return 0;
    l->started=l->active=1;
    for(unsigned i=3;i<7;i++) {
        if(!marble_audio_pins_amps(o))goto fail;
        if(i<5 && !marble_audio_pins_write(o,l,i,1,2))goto fail;
        if(!marble_audio_pins_write(o,l,i,0,marble_audio_pins_cfg[i-3]) ||
           !o->read(o->cookie,i,&c,&v) || c!=marble_audio_pins_cfg[i-3] || ((v^(i<5?2:l->io[i]))&2))goto fail;
    }
    if(!marble_audio_pins_amps(o))goto fail;
    return 1;
fail:l->failed=1;return 0;
}
static int marble_audio_pins_end(const MarbleAudioPinOps *o,MarbleAudioPins *l)
{
    if(!o || !l || !l->active || !marble_audio_pins_amps(o))return 0;
    /* Do not overwrite a foreign configuration. A write failure may have left
     * either our active value or the saved value, both belong to this lease. */
    uint32_t c,v;
    for(unsigned i=3;i<7;i++)if(!o->read(o->cookie,i,&c,&v) ||
       (c!=l->cfg[i] && c!=marble_audio_pins_cfg[i-3]))return 0;
    for(unsigned i=7;i-->3;) {
        if((l->cfg_mask&(1u<<i)) && !marble_audio_pins_write(o,l,i,0,l->cfg[i]))return 0;
        if((l->io_mask&(1u<<i)) && !marble_audio_pins_write(o,l,i,1,l->io[i]&2))return 0;
    }
    for(unsigned i=0;i<7;i++)if(!o->read(o->cookie,i,&c,&v) || c!=l->cfg[i] || ((v^l->io[i])&2))return 0;
    l->active=0;l->restored=1;return marble_audio_pins_amps(o);
}
#endif
