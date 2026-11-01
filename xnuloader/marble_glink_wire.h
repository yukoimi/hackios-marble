/* Bounded GLINK framing, adapted to fixed snapshots from the pinned Xiaomi
 * qcom_glink_native.c and HakiOS glink_pump. No MMIO, allocation or callbacks. */
#ifndef MARBLE_GLINK_WIRE_H
#define MARBLE_GLINK_WIRE_H
typedef struct {
    uint16_t command,channel;
    uint32_t parameter,bytes;
    char name[33];
} MarbleGlinkControl;
typedef struct {uint32_t bytes,id;} MarbleGlinkRemoteIntent;
static int marble_glink_intent_accept(MarbleGlinkRemoteIntent *table,unsigned capacity,unsigned *count,
    uint32_t id,uint32_t bytes,unsigned *index,unsigned *added)
{
    if(!table || !count || !index || !added || !capacity || capacity>512 || *count>capacity || !bytes || bytes>65536)return 0;
    for(unsigned i=0;i<*count;i++)if(table[i].id==id) {
        if(table[i].bytes!=bytes)return 0;*index=i;*added=0;return 1;
    }
    if(*count==capacity)return 0;
    *index=*count;*added=1;table[*count].id=id;table[*count].bytes=bytes;(*count)++;return 1;
}
static uint32_t marble_glink_le32(const uint8_t *p)
{return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static int marble_glink_ring(uint32_t size,uint32_t head,uint32_t tail,uint32_t *available)
{
    if(!available || size<1024 || size>65536 || (size|head|tail)&7 || head>=size || tail>=size)return 0;
    *available=head>=tail?head-tail:size-tail+head;return 1;
}
static int marble_glink_copy(const uint8_t *fifo,uint32_t size,uint32_t head,uint32_t tail,
    uint32_t offset,uint8_t *out,uint32_t bytes)
{
    uint32_t available=0;
    if(!fifo || !out || !marble_glink_ring(size,head,tail,&available) || offset>available || bytes>available-offset)return 0;
    uint32_t start=tail+offset;if(start>=size)start-=size;
    for(uint32_t i=0;i<bytes;i++) {out[i]=fifo[start];if(++start==size)start=0;}
    return 1;
}
/* This bring-up profile accepts control traffic only. It has advertised no
 * intents and opened no local service, so DATA/INTENT/DONE are refused. Return
 * 0 for incomplete, -1 for unsupported/malformed, +1 for a complete message. */
static int marble_glink_frame(const uint8_t *data,uint32_t available,MarbleGlinkControl *out,unsigned apps)
{
    if(!data || !out || apps>2)return -1;
    if(available<8)return 0;
    MarbleGlinkControl m={0};uint32_t first=marble_glink_le32(data);
    m.command=(uint16_t)first;m.channel=(uint16_t)(first>>16);m.parameter=marble_glink_le32(data+4);m.bytes=8;
    switch(m.command) {
    case 0:case 1:
        if(m.channel!=1)return -1;
        break;
    case 2:
        if(!m.channel || m.parameter<2 || m.parameter>32)return -1;
        m.bytes=(8+m.parameter+7)&~7u;
        if(available<m.bytes)return 0;
        for(unsigned i=0;i<m.parameter;i++) {
            unsigned c=data[8+i];
            if(i+1==m.parameter) {if(c)return -1;}
            else if(!((c>='a' && c<='z') || (c>='A' && c<='Z') || (c>='0' && c<='9') || c=='_' || c=='-' || c=='.' || c=='/'))return -1;
            m.name[i]=(char)c;
        }
        break;
    case 3:case 11:case 15:
        if(!m.channel)return -1;
        break;
    case 4:
        if(!apps || !m.channel || m.parameter)return -1;
        break;
    case 5:
        if(!apps || !m.channel || !m.parameter || m.parameter>64)return -1;
        m.bytes=8+8*m.parameter;
        if(available<m.bytes)return 0;
        for(unsigned i=0;i<m.parameter;i++) {
            uint32_t size=marble_glink_le32(data+8+8*i),id=marble_glink_le32(data+12+8*i);
            if(!size || size>65536)return -1;
            for(unsigned j=0;j<i;j++)if(marble_glink_le32(data+12+8*j)==id)return -1;
        }
        break;
    case 6:case 14:
        if(apps!=2 || !m.channel)return -1;
        break;
    case 9:case 12:
        if(apps!=2 || !m.channel || m.parameter!=1)return -1;
        if(available<16)return 0;
        uint32_t chunk=marble_glink_le32(data+8),left=marble_glink_le32(data+12);
        if(!chunk || chunk>512 || left>512-chunk)return -1;
        m.bytes=(16+chunk+7)&~7u;
        if(available<m.bytes)return 0;
        break;
    case 13:
        if(m.channel || m.parameter)return -1;
        break;
    default:return -1;
    }
    *out=m;return 1;
}
static int marble_glink_control(const uint8_t *data,uint32_t available,MarbleGlinkControl *out)
{return marble_glink_frame(data,available,out,0);}
#endif
