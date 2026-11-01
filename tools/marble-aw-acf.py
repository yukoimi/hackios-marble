"""Validate the ROM's Awinic ACF before any driver can consume its profiles.

Pinned AW882xx data_type/bin_parse/monitor definitions: MiCode audio-kernel
7d174adfe5304b74889e0cf0e16132a049707710. Host only; no phone or register access.
"""
import argparse,hashlib,json,struct
from pathlib import Path

def u32(data,at):return struct.unpack_from('<I',data,at)[0]
def crc8(data):
 crc=0
 for byte in data:
  for _ in range(8):
   crc=((crc^0x18)>>1)|0x80 if (crc^byte)&1 else crc>>1
   byte>>=1
 return crc
def text(raw):return raw.rstrip(b'\0').decode('ascii')
def register_bin(data):
 if len(data)<64:raise ValueError('short register header')
 checksum,version,kind,fmt,size,ui=struct.unpack_from('<6I',data)
 if checksum!=sum(data[4:])&0xffffffff or version!=0x01000000 or kind or fmt!=1 or size!=len(data)-60:raise ValueError('register header/checksum')
 if u32(data,32)!=2 or u32(data,36)!=2:raise ValueError('unsupported register width')
 count=u32(data,60)
 if not count or count>256 or 64+count*4!=len(data):raise ValueError('register count/length')
 regs=[dict(address=a,value=v) for a,v in struct.iter_unpack('<HH',data[64:])]
 if any(r['address']>=0x7e for r in regs):raise ValueError('register address outside 2032 map')
 return dict(chip=text(data[24:32]),ui=ui,register_count=count,registers=regs)
def monitor_bin(data):
 if len(data)<84 or u32(data,4)!=0x00010000 or u32(data,0)!=sum(data[4:])&0xffffffff:raise ValueError('monitor version/header/checksum')
 spans=[];tables={}
 for name,at in (('temperature',52),('voltage',72)):
  count,width,offset=struct.unpack_from('<3I',data,at)
  if not count or count>64 or width!=12 or offset<84 or offset>len(data) or count*width>len(data)-offset:raise ValueError('monitor table bounds')
  span=(offset,offset+count*width)
  if any(span[0]<b and a<span[1] for a,b in spans):raise ValueError('monitor table overlap')
  spans.append(span);tables[name]=[dict(zip(('minimum','maximum','ipeak','gain','vmax'),v)) for v in struct.iter_unpack('<hhHHI',data[span[0]:span[1]])]
 if sorted(spans)[0][0]!=84 or sorted(spans)[0][1]!=sorted(spans)[1][0] or sorted(spans)[1][1]!=len(data):raise ValueError('monitor unaccounted bytes')
 return dict(chip=text(data[8:16]),switch=u32(data,20),interval_ms=u32(data,24),tables=tables)
def inspect(data):
 if not 80<=len(data)<=1024*1024:raise ValueError('ACF size')
 magic,project,customer,version,author,table_size,count,offset,hdr,*reserved=struct.unpack_from('<I24s16s4s8I',data)
 if magic!=0x0a15f908 or hdr!=0x01000000 or offset!=80 or not 0<count<=64 or table_size!=count*96 or table_size>len(data)-offset:raise ValueError('ACF header/table geometry')
 end=offset+table_size;spans=[];entries=[];seen=set()
 for i in range(count):
  row=struct.unpack_from('<I16s4H4I32sI4I',data,offset+96*i)
  kind,name,index,bus,address,profile,section,size,at,crc,label,chip,*_=row
  if kind not in (0,2) or index>1 or chip!=0x2032 or profile not in (0,1,10) or section not in (4,10):raise ValueError('unsupported board device/profile/section')
  if (kind==2 and (bus,address)!=(65535,65535)) or (kind==0 and (bus!=2 or address not in (0x34,0x35))):raise ValueError('ACF device identity')
  if not size or at<end or at>len(data) or size>len(data)-at:raise ValueError('ACF payload bounds')
  span=(at,at+size)
  if any(span[0]<b and a<span[1] for a,b in spans):raise ValueError('ACF overlapping payload')
  spans.append(span);payload=data[at:at+size]
  if crc>255 or crc8(payload)!=crc:raise ValueError('ACF CRC8')
  key=(kind,index,bus,address,profile,section)
  if key in seen:raise ValueError('ambiguous duplicate profile')
  seen.add(key)
  if text(name)!='aw88263S' or text(label)!={0:'Music',1:'Voice',10:'Receiver'}[profile]:raise ValueError('profile name/id disagreement')
  decoded=register_bin(payload) if section==4 else monitor_bin(payload)
  if decoded['chip']!='AW88263S':raise ValueError('nested silicon identity')
  entries.append(dict(type=kind,index=index,bus=bus,address=address,profile=profile,name=text(label),chip_id=chip,
   section=section,offset=at,bytes=size,crc8=crc,sha256=hashlib.sha256(payload).hexdigest(),decoded=decoded))
 spans.sort()
 if spans[0][0]!=end or spans[-1][1]!=len(data) or any(a[1]!=b[0] for a,b in zip(spans,spans[1:])):raise ValueError('unaccounted ACF payload bytes')
 return dict(valid=True,sha256=hashlib.sha256(data).hexdigest(),project=text(project),customer=text(customer),version=list(reversed(version)),author=author,
  entries=entries,scope='offline board-specific firmware validation; physical chip ID, I2C, GPIO, DSP topology and audio output remain unverified',hardware_writes=0,audio_acceptance=False)
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('acf',type=Path);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
 r=inspect(a.acf.read_bytes());a.out.write_text(json.dumps(r,indent=2)+'\n');print('Validated',len(r['entries']),'ACF sections; no hardware access')
if __name__=='__main__':main()
