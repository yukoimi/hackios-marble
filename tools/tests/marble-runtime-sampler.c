#define XNU_MARBLE_AUDIO_RUNTIME_SAMPLING 1
#define main legacy_sampler_test
#include "marble-tpl-sampler.c"
#undef main
int main(void){
legacy_sampler_test();
reset();assert(marble_tpl_sampler_prepare(0x80000000));assert(marble_tpl_sampler_arm());
for(unsigned i=0;i<53;i++){clock_us+=10000;notify((void *)0x1234,0);}
assert(marble_tpl_sampler.count==53 && marble_tpl_sampler.stacks==0);
for(unsigned i=53;i<90;i++){clock_us+=10000;notify((void *)0x1234,0);}
assert(marble_tpl_sampler.stacks==2 && marble_tpl_sampler.stack[0].index==59 && marble_tpl_sampler.stack[1].index==89);
assert(marble_tpl_sampler_release() && cancels==1 && closes==1);
puts("PASS runtime scope: no stack at 530ms, bounded snapshots at indices59/89, cancel/close once before PAS");
return 0;}
