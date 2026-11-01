#include <stdint.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include "../../xnuloader/marble_aw2032_digital.h"
static unsigned marble_i2c_amp_address,marble_i2c_amp_dirty,marble_i2c_amp_register_dirty;
static unsigned marble_i2c_amp_digital_authorized=1,marble_qup_keepalive_held=1,marble_pil_auth_attempted=1;
static unsigned marble_i2c_amp_analog_authorized;
static const uint64_t marble_i2c_amp_aux_addresses[]={0x88026c,0x880270,0x880614,0x88080c,0x880810,0x880814};
static uint32_t aux[6],gpio_cfg[2],gpio_io[2];
static uint16_t ctrl[2];static uint64_t now;
static unsigned operation,fail_at,events[32],event_count;
static int fail(void){return ++operation==fail_at;}
static void delay(unsigned us){now+=us;}
static struct {void (*stall)(unsigned);} bs={delay},*BS=&bs;
static uint64_t marble_i2c_amp_now(void *ctx){(void)ctx;return now;}
static void marble_i2c_amp_delay(void *ctx,unsigned us){(void)ctx;delay(us);}
static int marble_i2c_amp_digital_read(void *ctx,unsigned a,unsigned reg,uint16_t *v)
{
 (void)ctx;assert(a==marble_i2c_amp_address && (a==0x34 || a==0x35));
 if(fail())return 1;
 unsigned i=a-0x34;
 *v=reg==4?ctrl[i]:reg==5?0xf039:(ctrl[i]&1)?0:(ctrl[i]&2)?0x11:0x311;
 return 0;
}
static int write_reg(unsigned a,unsigned reg,uint16_t value)
{
 assert(a==marble_i2c_amp_address && reg==4);ctrl[a-0x34]=value;
 /* Fault after mutation tests partial-write cleanup. */
 return fail();
}
static int marble_i2c_amp_digital_write(void *ctx,unsigned a,unsigned reg,uint16_t v)
{(void)ctx;assert(marble_aw2032_digital_allowed(a,reg,v));return write_reg(a,reg,v);}
static int marble_i2c_amp_analog_write(void *ctx,unsigned a,unsigned reg,uint16_t v)
{(void)ctx;assert(marble_i2c_amp_analog_authorized);return write_reg(a,reg,v);}
typedef struct {void *ctx;int (*read)(void *,uint64_t,uint32_t *);int (*write)(void *,uint64_t,uint32_t);uint64_t (*now)(void *);void (*delay)(void *,unsigned);} MarbleI2cFifoOps;
static int marble_i2c_amp_read(void *ctx,uint64_t a,uint32_t *v){(void)ctx;(void)a;*v=0;return 0;}
static int marble_i2c_amp_write(void *ctx,uint64_t a,uint32_t v){(void)ctx;(void)a;(void)v;return 0;}
static int marble_i2c_fifo_clean(const MarbleI2cFifoOps *o){(void)o;return 1;}
static uint32_t read_mmio(uint64_t a)
{
 for(unsigned i=0;i<6;i++)if(a==marble_i2c_amp_aux_addresses[i])return aux[i];
 unsigned i=a>=0xf178000?1:0;return (a&4)?gpio_io[i]:gpio_cfg[i];
}
static int marble_i2c_amp_store(uint64_t a,uint32_t v,const char *phase)
{
 assert(!marble_i2c_amp_dirty);(void)phase;
 for(unsigned i=0;i<6;i++)if(a==marble_i2c_amp_aux_addresses[i]){aux[i]=v;return fail();}
 unsigned i=marble_i2c_amp_address-0x34;
 if(a&4){gpio_io[i]=v;events[event_count++]=0x100+marble_i2c_amp_address;}else gpio_cfg[i]=v;
 return fail();
}
#define print(...) ((void)0)
#define MARBLE_I2C_BOARD_READ32 read_mmio
#include "../../xnuloader/marble_i2c_board_held_fw.h"
static void setup(void)
{
 memset(marble_i2c_board_device,0,sizeof(marble_i2c_board_device));
 marble_i2c_amp_dirty=marble_i2c_amp_register_dirty=0;
 marble_i2c_board_held_requested=1;operation=fail_at=event_count=0;now=0;
 ctrl[0]=0xb443;ctrl[1]=0x3443;
 for(unsigned i=0;i<2;i++) {
  gpio_cfg[i]=0x201;gpio_io[i]=2;
  marble_i2c_board_device[i].prepared=marble_i2c_board_device[i].reset_touched=1;
  marble_i2c_board_device[i].cfg=1;marble_i2c_board_device[i].aux_changed=63;
  for(unsigned j=0;j<6;j++) {aux[j]=100+j;marble_i2c_board_device[i].saved[j]=i?100+j:j;}
 }
}
static int begin(void)
{
 marble_i2c_amp_address=0x34;if(!marble_i2c_board_begin(0x34))return 0;
 marble_i2c_amp_address=0x35;return marble_i2c_board_begin(0x35);
}
int main(void)
{
 setup();assert(begin() && marble_i2c_board_check());now+=43000;
 assert(marble_i2c_board_finish() && !marble_i2c_amp_dirty);
 assert(ctrl[0]==0xb443 && ctrl[1]==0x3443 && gpio_cfg[0]==1 && gpio_cfg[1]==1 && !gpio_io[0] && !gpio_io[1]);
 assert(event_count==2 && events[0]==0x135 && events[1]==0x134);
 for(unsigned i=0;i<6;i++)assert(aux[i]==i);
 unsigned total=operation;
 for(unsigned at=1;at<=total;at++) {
  setup();fail_at=at;unsigned begun=begin();
  if(begun)(void)marble_i2c_board_check();
  (void)marble_i2c_board_finish();
  /* Every fault still attempts both reset assertions. No global dirty
   * latch is allowed to prevent cleanup of the second device. */
  assert(event_count==2 && events[0]==0x135 && events[1]==0x134);
  assert(!gpio_io[0] && !gpio_io[1]);
 }
 setup();assert(begin());now+=600000;assert(!marble_i2c_board_check());
 assert(!marble_i2c_board_finish() && marble_i2c_amp_dirty && !gpio_io[0] && !gpio_io[1]);
 puts("PASS bound board lifecycle: both muted through DATA, reverse shutdown/AUX restore, every partial write/read failure attempts both resets, deadline retains dirty leases; no hardware acceptance");
 return 0;
}
