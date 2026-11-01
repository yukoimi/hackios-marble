#include "../../xnuloader/marble_brightness_mailbox.h"
#include <assert.h>
#include <stdio.h>
int main(void)
{
    assert(marble_brightness_request_valid(2,2,0,0,0));
    assert(marble_brightness_request_valid(4,4,2,1000,0));
    assert(!marble_brightness_request_valid(0,0,0,500,0));
    assert(!marble_brightness_request_valid(3,3,0,500,0));
    assert(!marble_brightness_request_valid(2,4,0,500,0));
    assert(!marble_brightness_request_valid(2,2,2,500,0));
    assert(!marble_brightness_request_valid(2,2,4,500,0));
    assert(!marble_brightness_request_valid(2,2,0,1001,0));
    assert(!marble_brightness_request_valid(2,2,0,500,1));
    for(unsigned original=32;original<=4095;original++) {
        unsigned minimum=original>327?328:8,last=minimum;
        if(minimum>=original)continue;
        for(unsigned milli=0;milli<=1000;milli++) {
            unsigned level=marble_brightness_target(original,minimum,milli);
            assert(level>=last && level<=original && level>=minimum);
            assert((level>327)==(original>327));last=level;
        }
        assert(last==original);
    }
    assert(!marble_brightness_target(4096,328,500));
    assert(!marble_brightness_target(500,8,500));
    assert(!marble_brightness_target(200,328,500));
    assert(!marble_brightness_target(500,328,1001));
    puts("brightness mailbox: reject torn/replayed/reserved requests; 4 million bounded monotonic mappings PASS");
}
