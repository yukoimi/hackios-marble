/* Complete-format dictionary is unpacked once, before any peripheral startup.
 * Standard LZ4_decompress_safe bounds both source and destination. No heap. */
#include "marble_log_formats.h"
extern int LZ4_decompress_safe(const char *,char *,int,int);
static char marble_format_raw[MARBLE_FORMAT_RAW_SIZE];
static unsigned marble_format_ready;
static int marble_format_prepare(void)
{
    if(marble_format_ready)return 1;
    int n=LZ4_decompress_safe((const char *)marble_format_block,marble_format_raw,
        sizeof(marble_format_block),sizeof(marble_format_raw));
    if(n!=sizeof(marble_format_raw))return 0;
    for(unsigned i=0;i<MARBLE_FORMAT_COUNT;i++) {
        unsigned at=marble_format_offsets[i];
        if(at>=sizeof(marble_format_raw))return 0;
        while(at<sizeof(marble_format_raw) && marble_format_raw[at])at++;
        if(at==sizeof(marble_format_raw))return 0;
    }
    marble_format_ready=1;return 1;
}
static const char *marble_format_get(const char *fmt)
{
    if((unsigned char)fmt[0]!=2)return fmt;
    unsigned index=(unsigned char)fmt[1]|(unsigned)(unsigned char)fmt[2]<<8;
    if(!marble_format_ready || index>=MARBLE_FORMAT_COUNT)return "invalid compact log format";
    return marble_format_raw+marble_format_offsets[index];
}
