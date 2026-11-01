/* Status must remain available even when the diagnostic log is full. */
#ifndef XNULOADER_RESULT_H
#define XNULOADER_RESULT_H
#include <stddef.h>
#include <stdint.h>
static void loader_format_result(char *out,size_t capacity,const char *what,uint64_t a,uint64_t b)
{
    if(!capacity)return;
    size_t at=0;
#define RESULT_CHAR(c) do {char value_=(c);if(at+1<capacity)out[at++]=value_;} while(0)
    for(;*what;what++)RESULT_CHAR(*what);
    for(unsigned field=0;field<2;field++) {
        RESULT_CHAR(' ');char digits[24];unsigned count=0,base=field?16:10;
        uint64_t value=field?b:a;
        do {digits[count++]="0123456789abcdef"[value%base];value/=base;}while(value);
        while(count)RESULT_CHAR(digits[--count]);
    }
    out[at]=0;
#undef RESULT_CHAR
}
#endif
