"""Decode bounded in-band digital PLL evidence; never claim PAS/audio acceptance."""
import argparse,hashlib,importlib.util,json,re
from pathlib import Path
def decode(text):
 data=bytearray();end=None
 for original in text.splitlines():
  line=original.strip()
  if line.startswith('(bootloader) '):line=line[len('(bootloader) '):]
  if line=='ADC-OVERFLOW':raise ValueError('firmware capture overflow')
  if line.startswith('ADC:'):
   m=re.fullmatch(r'ADC:([0-9a-f]{8}):([0-9a-f]{2,40})',line)
   if not m or len(m[2])%2 or end is not None:raise ValueError('malformed/late capture frame')
   offset=int(m[1],16);chunk=bytes.fromhex(m[2])
   if offset!=len(data) or len(data)+len(chunk)>32768:raise ValueError('capture gap/duplicate/order/bound')
   data.extend(chunk)
  elif line.startswith('ADC-END:'):
   m=re.fullmatch(r'ADC-END:([0-9a-f]{8}):([0-9a-f]{16})',line)
   if not m or end is not None:raise ValueError('missing/duplicate capture terminator')
   end=(int(m[1],16),int(m[2],16))
 if not data or end is None or end[0]!=len(data):raise ValueError('capture length/terminator incomplete')
 h=0xcbf29ce484222325
 for byte in data:h=((h^byte)*0x100000001b3)&0xffffffffffffffff
 if h!=end[1]:raise ValueError('capture checksum mismatch')
 decoded=data.decode('ascii')
 if not decoded.endswith('\n'):raise ValueError('capture record truncated')
 return decoded
def inspect(text):
 s=importlib.util.spec_from_file_location('digital_capture_report',Path(__file__).with_name('marble-i2c-inventory-report.py'))
 m=importlib.util.module_from_spec(s);s.loader.exec_module(m);errors=[];devices=[]
 try:
  writes=[dict(re.findall(r'(\w+)=([^\s]+)',l)) for l in text.splitlines() if l.startswith('marble-i2c-amp: write ')]
  for address in (0x34,0x35):
   d=f'{address:x}';digital=[r for r in writes if r.get('device')==d and r.get('phase') in ('digital-write','digital-read')]
   m._inspect_aw2032_digital(text,address,digital)
   def one(prefix):
    rows=[dict(re.findall(r'(\w+)=([^\s]+)',l)) for l in text.splitlines() if l.startswith(prefix) and f'device={d} ' in l]
    if len(rows)!=1:raise ValueError('capture missing/duplicate '+prefix+d)
    return rows[0]
   amp=one('marble-i2c-amp: complete ')
   for k,v in dict(PASS=1,chip_id='2032',reset_restored=1,bus_clean=1,aux_restored=1,dirty=0,amplifier_register_writes=48,speaker_switch_writes=0,audio_acceptance=0).items():
    if amp.get(k)!=str(v):raise ValueError('captured amplifier reset/bus contract '+k)
   gpio=0xf101000 if address==0x34 else 0xf178000
   resets=[r for r in writes if r.get('device')==d and r.get('phase') in ('reset-assert','reset-restore')]
   if [(r['phase'],int(r['address'],16),int(r['value'],16)) for r in resets]!=[('reset-assert',gpio+4,0),('reset-restore',gpio,1)]:raise ValueError('captured physical reset sequence')
   devices.append(dict(address=address,PLL_locked=True,original_registers_restored=True,hardware_reset_asserted=True))
 except (KeyError,ValueError,UnicodeError) as e:errors.append(str(e))
 return dict(passed=not errors,errors=errors,devices=devices,PAS_shutdown_verified=False,audio_acceptance=False,
  scope='in-band digital PLL/register/MMIO and amplifier reset evidence only; DSP power/ROM retained until mandatory cold reset; no playback or iOS HAL acceptance')
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--raw',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
 raw=a.raw.read_bytes();a.out.mkdir(parents=True,exist_ok=True)
 try:
  text=decode(raw.decode(errors='replace'));r=inspect(text);(a.out/'digital-inband.log').write_text(text,encoding='ascii')
  r['payload_sha256']=hashlib.sha256(text.encode('ascii')).hexdigest()
 except (ValueError,UnicodeError) as e:r=dict(passed=False,errors=[str(e)],PAS_shutdown_verified=False,audio_acceptance=False)
 r['raw_sha256']=hashlib.sha256(raw).hexdigest();(a.out/'report.json').write_text(json.dumps(r,indent=2)+'\n');print(json.dumps(r,indent=2));raise SystemExit(not r['passed'])
if __name__=='__main__':main()
