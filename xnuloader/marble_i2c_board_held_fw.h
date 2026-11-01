/* Bound muted amplifier lifecycle; included after the qualified FIFO adapters. */
#ifndef MARBLE_I2C_BOARD_HELD_FW_H
#define MARBLE_I2C_BOARD_HELD_FW_H
#ifndef MARBLE_I2C_BOARD_READ32
#define MARBLE_I2C_BOARD_READ32(a) (*(volatile uint32_t *)(uintptr_t)(a))
#endif
#include "marble_aw2032_held.h"
static unsigned marble_i2c_board_held_requested;
static struct {
    unsigned prepared,reset_touched,aux_changed,bus_dirty;
    uint32_t cfg,saved[6];
    MarbleAwHeld held;
} marble_i2c_board_device[2];
static int marble_i2c_board_write(void *ctx,unsigned address,unsigned reg,uint16_t value)
{
    if(marble_aw2032_digital_allowed(address,reg,value))return marble_i2c_amp_digital_write(ctx,address,reg,value);
    return marble_i2c_amp_analog_write(ctx,address,reg,value);
}
static int marble_i2c_board_begin(unsigned address)
{
    unsigned index=address==0x34?0:1;
    if(!marble_i2c_board_held_requested || !marble_i2c_amp_digital_authorized ||
       !marble_qup_keepalive_held || !marble_pil_auth_attempted || marble_i2c_amp_dirty)return 0;
    MarbleAwDigitalOps o={NULL,marble_i2c_amp_digital_read,marble_i2c_board_write,marble_i2c_amp_delay};
    marble_i2c_amp_analog_authorized=1;
    int ok=marble_aw2032_held_begin(&o,address,marble_i2c_amp_now,&marble_i2c_board_device[index].held);
    marble_i2c_amp_analog_authorized=0;
    print("aud-board-held: phase=begin device=%x PASS=%d status=%x HMUTE=1 attenuation_db=90 audible=0\n",
        (uint64_t)address,(uint64_t)ok,(uint64_t)marble_i2c_board_device[index].held.status);
    return ok;
}
static int marble_i2c_board_check(void)
{
    unsigned ok=1;
    for(unsigned i=0;i<2;i++) {
        unsigned address=0x34+i;marble_i2c_amp_address=address;
        MarbleAwDigitalOps o={NULL,marble_i2c_amp_digital_read,marble_i2c_board_write,marble_i2c_amp_delay};
        if(!marble_aw2032_held_check(&o,address,marble_i2c_amp_now,&marble_i2c_board_device[i].held))ok=0;
    }
    return ok;
}
/* Do not latch global dirty before both reset assertions have been tried.
 * Device 35 saved AUX after 34's setup, so unwind in reverse order. */
static int marble_i2c_board_finish(void)
{
    unsigned all=1;
    for(int i=1;i>=0;i--) {
        if(!marble_i2c_board_device[i].prepared)continue;
        unsigned address=0x34+i;marble_i2c_amp_address=address;
        uint64_t gpio=0xf100000ULL+(address==0x34?1:120)*0x1000ULL;
        MarbleAwDigitalOps o={NULL,marble_i2c_amp_digital_read,marble_i2c_board_write,marble_i2c_amp_delay};
        marble_i2c_amp_analog_authorized=1;
        unsigned down=marble_aw2032_held_finish(&o,address,marble_i2c_amp_now,&marble_i2c_board_device[i].held);
        marble_i2c_amp_analog_authorized=0;
        unsigned reset=1;
        if(marble_i2c_board_device[i].reset_touched) {
            reset=!marble_i2c_amp_store(gpio+4,0,"reset-assert");BS->stall(1000);
            /* Still attempt cfg restoration if reset assertion failed. */
            unsigned cfg=!marble_i2c_amp_store(gpio,marble_i2c_board_device[i].cfg,"reset-restore");
            reset=reset && cfg && MARBLE_I2C_BOARD_READ32(gpio)==marble_i2c_board_device[i].cfg &&
                !(MARBLE_I2C_BOARD_READ32(gpio+4)&2);
        }
        MarbleI2cFifoOps fifo={NULL,marble_i2c_amp_read,marble_i2c_amp_write,marble_i2c_amp_now,marble_i2c_amp_delay};
        unsigned clean=!marble_i2c_board_device[i].bus_dirty && !marble_i2c_amp_register_dirty && marble_i2c_fifo_clean(&fifo);
        unsigned aux=clean;
        if(clean)for(int j=5;j>=0;j--)if(marble_i2c_board_device[i].aux_changed&(1u<<j)) {
            if(marble_i2c_amp_store(marble_i2c_amp_aux_addresses[j],marble_i2c_board_device[i].saved[j],"restore") ||
               MARBLE_I2C_BOARD_READ32(marble_i2c_amp_aux_addresses[j])!=marble_i2c_board_device[i].saved[j])aux=0;
        }
        clean=clean && aux && marble_i2c_fifo_clean(&fifo);
        unsigned ok=down && reset && clean && !marble_i2c_board_device[i].held.failed;
        print("aud-board-held: phase=finish device=%x PASS=%d powered_down=%d reset_restored=%d bus_clean=%d aux_restored=%d held_us=%d HMUTE=1 audible=0\n",
            (uint64_t)address,(uint64_t)ok,(uint64_t)down,(uint64_t)reset,(uint64_t)clean,(uint64_t)aux,marble_i2c_board_device[i].held.held_us);
        if(!ok)all=0;
    }
    if(!all)marble_i2c_amp_dirty=1;
    return all;
}
#endif
