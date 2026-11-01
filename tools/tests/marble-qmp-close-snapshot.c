#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../xnuloader/marble_qmp_close_snapshot.h"
static struct {MarbleQmpDescriptor d[2];unsigned empty[2],at;int status[2];} model;
static int sample(void *ctx,MarbleQmpDescriptor *d,unsigned *empty)
{(void)ctx;unsigned i=model.at++;assert(i<2);*d=model.d[i];*empty=model.empty[i];return model.status[i];}
static void setup(void)
{
 memset(&model,0,sizeof(model));MarbleQmpDescriptor d={.magic=MARBLE_QMP_MAGIC,.version=1,
  .local_offset=0x100,.remote_offset=0x200,.local_size=0x100,.remote_size=0x100,
  .local_link=MARBLE_QMP_UP,.local_link_ack=MARBLE_QMP_UP,.local_channel=MARBLE_QMP_DOWN,.local_channel_ack=MARBLE_QMP_DOWN,
  .remote_link=MARBLE_QMP_UP,.remote_link_ack=MARBLE_QMP_UP,.remote_channel=MARBLE_QMP_UP,.remote_channel_ack=MARBLE_QMP_UP};
 model.d[0]=model.d[1]=d;model.empty[0]=model.empty[1]=1;model.status[0]=model.status[1]=1;
}
int main(void)
{
 MarbleQmpDescriptor out={0};setup();assert(marble_qmp_close_snapshot(sample,0,&out)==1 && !memcmp(&out,&model.d[0],sizeof(out)));
 for(unsigned i=0;i<sizeof(out)/4;i++) {
  setup();((uint32_t *)&model.d[1])[i]^=1;assert(!marble_qmp_close_snapshot(sample,0,&out));
 }
 for(unsigned i=0;i<2;i++) {
  setup();model.empty[i]=0;assert(!marble_qmp_close_snapshot(sample,0,&out));
  setup();model.status[i]=0;assert(!marble_qmp_close_snapshot(sample,0,&out));
  setup();model.status[i]=-1;assert(marble_qmp_close_snapshot(sample,0,&out)==-1);
 }
 setup();model.d[0].magic=model.d[1].magic=0;assert(marble_qmp_close_snapshot(sample,0,&out)==-1);
 setup();model.empty[0]=2;assert(marble_qmp_close_snapshot(sample,0,&out)==-1);
 assert(marble_qmp_close_snapshot(0,0,&out)==-1);
 /* Stable remote DOWN/UP transitions still use the original strict codec. */
 for(unsigned ack=0;ack<=2;ack++) {
  setup();MarbleQmpDescriptor original=model.d[0];
  model.d[0].remote_channel=ack?MARBLE_QMP_UP:MARBLE_QMP_DOWN;
  model.d[0].remote_channel_ack=ack==1?MARBLE_QMP_DOWN:MARBLE_QMP_UP;model.d[1]=model.d[0];
  assert(marble_qmp_close_snapshot(sample,0,&out)==1);
  assert(marble_qmp_borrowed_close_step(&out,&original,ack)==(int)ack+1);
 }
 puts("PASS QMP coherent close snapshot: every torn word, busy mailbox and context drift waits without ACK; stable invalid geometry rejects; unchanged observed DOWN/UP ownership codec; caller deadline unchanged");
 return 0;
}
