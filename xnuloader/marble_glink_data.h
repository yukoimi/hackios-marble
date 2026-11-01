/* Single owned 512-byte reusable intent. Fixed Xiaomi GLINK DATA/CONT layout.
 * No allocator, arbitrary channel, unchecked length or overlapping packet. */
#ifndef MARBLE_GLINK_DATA_H
#define MARBLE_GLINK_DATA_H
#include "marble_glink_wire.h"
typedef struct {unsigned used,left,active;uint8_t data[512];} MarbleGlinkAssembly;
static int marble_glink_fragment(MarbleGlinkAssembly *s,unsigned command,
    const uint8_t *frame,unsigned bytes,unsigned channel)
{
    if(!s || !frame || bytes<16 || (command!=9 && command!=12) ||
       marble_glink_le32(frame)!=(command|(channel<<16)) ||
       marble_glink_le32(frame+4)!=1)return 0;
    unsigned n=marble_glink_le32(frame+8),left=marble_glink_le32(frame+12);
    if(!n || n>512 || left>512-n || bytes!=((16+n+7)&~7u) ||
       (command==9 && (s->active || s->used)) ||
       (command==12 && (!s->active || n>s->left || left!=s->left-n)) ||
       s->used>512-n)return 0;
    for(unsigned i=0;i<n;i++)s->data[s->used+i]=frame[16+i];
    s->used+=n;s->left=left;s->active=left!=0;return 1;
}
#endif
