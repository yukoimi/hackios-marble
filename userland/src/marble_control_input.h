/* Qualified overlay classification only; never alter coordinates or releases. */
#ifndef MARBLE_CONTROL_INPUT_H
#define MARBLE_CONTROL_INPUT_H
#include <stdint.h>
#define MARBLE_CONTROL_VISIBILITY "com.marble.ui.control-visible"
static inline int marble_control_bottom_is_local(uint64_t state,uint64_t now_us,uint32_t y)
{
    uint64_t stamp=state>>1;
    return (state&1) && stamp && now_us>=stamp && now_us-stamp<=2000000 && y<=65535 &&
        (uint64_t)y*812>65535ULL*(812-34);
}
#endif
