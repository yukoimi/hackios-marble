#include <assert.h>
#include <stdio.h>
#include "../../xnuloader/marble_audio_runtime_bound.h"
int main(void){
assert(!marble_audio_runtime_bound(299999,1,1));
assert(marble_audio_runtime_bound(300000,1,1));
assert(marble_audio_runtime_bound(599999,1,1));
assert(!marble_audio_runtime_bound(600000,1,1));
assert(!marble_audio_runtime_bound(1529933,1,1));
assert(!marble_audio_runtime_bound(530000,0,1));
assert(!marble_audio_runtime_bound(530000,1,0));
assert(!marble_audio_runtime_bound(UINT64_MAX,1,1));
puts("PASS actual runtime including cleanup: [300000,600000), completion and thermal required; reject observed overrun");}
