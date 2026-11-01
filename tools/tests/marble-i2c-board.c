#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../xnuloader/marble_i2c_board.h"
int main(void)
{
    MarbleI2cBoardConfig c=marble_i2c_board_candidate();assert(marble_i2c_board_candidate_valid(&c));
    assert(c.core_base+c.offset==0x880000 && (c.scl>>4&1023)==57 && (c.sda>>4&1023)==56);
    assert(!strcmp(c.common_clocks[0],"gcc_qupv3_wrap2_core_clk") && !strcmp(c.se_clocks[0],"gcc_qupv3_wrap2_s0_clk"));
    assert(!c.common_clocks[4] && !c.se_clocks[1]);
    assert(c.timings[0].se_khz==19200 && c.timings[0].bus_khz==100 && c.timings[0].divider==7 && c.timings[0].cycle==26 && c.timings[0].high==10 && c.timings[0].low==11);
    for(unsigned i=0;i<sizeof(c);i++) {
        MarbleI2cBoardConfig bad=c;((uint8_t *)&bad)[i]^=1;
        assert(!marble_i2c_board_candidate_valid(&bad));
    }
    assert(!marble_i2c_board_candidate_valid(NULL));
    puts("PASS I2C board: 88-byte native layout/offsets, physical region, pins and ROM 100 kHz timing; rejects each mutated byte; no hardware invocation");
}
