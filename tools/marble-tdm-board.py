"""Offline TDM contract from the actual ROM module, Android FDT and ACF.

Does not open a device, configure pins/clocks or grant playback acceptance.
"""
import argparse,hashlib,importlib.util,json,struct
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
MACHINE_SHA='11aff0d910e2f62cf5123f85cb1467f7d167818c677d60c012c99ee209993a90'
FDT_SHA='0973dd78214906911189bb3cb8fcab718f3a08aa9d0eca8df763b05347cea973'
def sha(b):return hashlib.sha256(b).hexdigest()
def module(name,path):
 s=importlib.util.spec_from_file_location(name,path);m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m
def section(b,name):
 if b[:6]!=b'\x7fELF\x02\x01':raise ValueError('ELF identity')
 off=struct.unpack_from('<Q',b,40)[0];size,n,idx=struct.unpack_from('<HHH',b,58)
 if size!=64 or not 0<n<=4096 or idx>=n or off+64*n>len(b):raise ValueError('ELF section bounds')
 rows=[struct.unpack_from('<IIQQQQIIQQ',b,off+64*i) for i in range(n)]
 def data(row):
  if row[4]+row[5]>len(b):raise ValueError('ELF content bounds')
  return b[row[4]:row[4]+row[5]]
 names=data(rows[idx]);found=[r for r in rows if r[0]<len(names) and names[r[0]:].split(b'\0')[0]==name.encode()]
 if len(found)!=1:raise ValueError('ELF section identity')
 return data(found[0])
def inspect(fdt,machine,rom_machine,acf):
 if sha(fdt)!=FDT_SHA or sha(machine)!=MACHINE_SHA or machine!=rom_machine:raise ValueError('unqualified ROM/runtime inputs')
 t=module('tdm_fdt',ROOT/'tools/marble-fdt.py').parse(fdt,maximum=1048576,maximum_nodes=8192,maximum_properties=65536)
 def cells(p,k):
  b=t[p][k]
  if len(b)%4:raise ValueError('DT cell size')
  return list(struct.unpack('>'+str(len(b)//4)+'I',b))
 sound='/soc/spf_core_platform/sound';pin='/soc/spf_core_platform/tert_tdm_pinctrl'
 if t[sound]['qcom,model']!=b'ukee-mtp-snd-card\0' or cells(sound,'qcom,tdm-max-slots')!=[8] or cells(sound,'qcom,tert-mi2s-gpios')!=cells(pin,'phandle'):raise ValueError('sound route identity')
 phandles={struct.unpack('>I',v['phandle'])[0]:p for p,v in t.items() if 'phandle' in v}
 if t[pin]['pinctrl-names']!=b'aud_active\0aud_sleep\0':raise ValueError('pin state identity')
 pins=[]
 for state in (0,1):
  references=cells(pin,'pinctrl-'+str(state))
  if len(references)!=4:raise ValueError('pin state count')
  for i,ref in enumerate(references):
   p=phandles[ref];mux=t[p+'/mux'];cfg=t[p+'/config'];gpio=(121,123,122,124)[i]
   function=('mi2s2_sck','mi2s2_ws','mi2s2_data0','mi2s2_data1')[i] if not state else 'gpio'
   if mux['pins']!=('gpio'+str(gpio)+'\0').encode() or mux['function']!=(function+'\0').encode() or cfg['pins']!=mux['pins'] or cells(p+'/config','drive-strength')!=[2 if state else 8]:raise ValueError('pin mux/electrical configuration')
   required=('bias-pull-down','input-enable') if state else (('bias-disable','output-high') if i<2 else ('bias-pull-down',) if i==3 else ('bias-disable',))
   if any(cfg.get(k)!=b'' for k in required):raise ValueError('pin bias/direction')
   pins.append(dict(state='sleep' if state else 'active',gpio=gpio,function=function,drive_ma=2 if state else 8,properties=list(required)))
 dai='/soc/qcom,msm-dai-tdm-tert-rx';expected=dict(data_delay=1,invert_sync=1,data_out=0,sync_src=1,sync_mode=1,clk_internal=1,clk_rate=1536000,group_port_id=0x9020,group_num_ports=1,group_id=0x9120)
 for k,v in expected.items():
  if cells(dai,'qcom,msm-cpudai-tdm-'+k.replace('_','-'))!=[v]:raise ValueError('TDM DAI property '+k)
 text=section(machine,'.text');ro=section(machine,'.rodata')
 # Matched instructions prove clamping and the smartpa-width branch in the
 # actual binary; do not silently use the older public source's constants.
 instructions={0x784:0x7100081f,0x788:0x52800408,0x78c:0x52800209,0x790:0x1a88013a,
  0x8c8:0xb941c29b,0x8e4:0x1b1a7f68,0x8e8:0x52800089,0x8ec:0x1b177d03,
  0x147c:0xb94002e8,0x1480:0x51000508,0x1484:0x7100111f,0x1488:0x54000143,0x1494:0x52800088,0x14a8:0xb90002e8}
 if any(struct.unpack_from('<I',text,k)[0]!=v for k,v in instructions.items()):raise ValueError('machine behavior changed')
 if ro[0x146a:].split(b'\0')[0]!=b'aw882xx' or ro[0x15a4:].split(b'\0')[0]!=b'fs19xx':raise ValueError('smartpa type mapping')
 clocks=struct.unpack_from('<6I',ro,0x60dc)
 if clocks[2]!=0x204:raise ValueError('tertiary clock ID')
 firmware=module('tdm_acf',ROOT/'tools/marble-aw-acf.py').inspect(acf)
 if firmware['sha256']!='77fa69a4915cf493300b63bb7911cd3cc7061fbc4c9319626c520f67556551d5':raise ValueError('ACF identity')
 profiles=[]
 for e in firmware['entries']:
  if 'registers' not in e['decoded']:continue
  regs={x['address']:x['value'] for x in e['decoded']['registers']};v=regs[6]
  if (v>>10)&3!=e['index']+1 or (v>>6)&3!=3 or v&15!=9:raise ValueError('ACF stereo/32-bit/96kHz format')
  profiles.append(dict(index=e['index'],profile=e['name'],i2sctrl_hex=f'{v:04x}',channel_select=(v>>10)&3,sample_bits=32,sample_rate=96000,rx_slot=(regs[7]>>8)&15,tx_slot=(regs[7]>>12)&15))
 if len(profiles)!=6:raise ValueError('ACF profile coverage')
 return dict(passed=True,input_sha256=dict(fdt=sha(fdt),machine=sha(machine),acf=sha(acf)),runtime_matches_user_ROM=True,
  tdm_pins=pins,device_tree=expected,dt_max_slots=8,driver_effective_slots=4,
  driver_slot_bits=dict(aw882xx_type_1=32,default_type_0=32,fs19xx_type_2=16),
  candidate_aw_clock=dict(rate=96000,slots=4,slot_bits=32,computed_bitclock_hz=12288000,PRM_clock_id=0x204,PRM_clock_attribute=4,clock_root=0),
  profiles=profiles,hardware_writes=0,audio_acceptance=False,
  remaining=['observe actual live smartpa type before clock programming','DSP graph create/close and qualified TDM configuration','speaker protection/calibration and amplifier lifecycle','XNU sound sink and observed playback'])
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--material',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
 r=inspect((a.material/'android-runtime.fdt').read_bytes(),(a.material/'machine_dlkm.ko').read_bytes(),(a.material/'rom-machine_dlkm.ko').read_bytes(),(ROOT/'out/audio-route-v103-plan/rom-aw882xx-acf.bin').read_bytes())
 a.out.write_text(json.dumps(r,indent=2)+'\n');print('Verified ROM/runtime TDM contract; zero hardware writes; audio acceptance false')
if __name__=='__main__':main()
