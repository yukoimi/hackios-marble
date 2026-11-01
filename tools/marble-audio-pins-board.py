"""Verify the actual cape FDT and pinned pinctrl definitions before mux writes."""
import argparse,hashlib,importlib.util,json,re,struct
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def inspect():
 base=ROOT/'out/audio-tdm-pins-v148-audit';header=(base/'pinctrl-cape.h').read_bytes()
 if hashlib.sha256(header).hexdigest()!='7bd55d53c148056cbcdc1f4a65d671f5ad4e458366c527cef568f6411a273297':raise ValueError('pinned cape source changed')
 text=header.decode();bits={'mux_bit':2,'pull_bit':0,'drv_bit':6,'oe_bit':9,'out_bit':1}
 for name,n in bits.items():
  if not re.search(r'\.'+name+r'\s*=\s*'+str(n)+r'\s*,',text):raise ValueError('cape field '+name)
 spec=importlib.util.spec_from_file_location('pins_fdt',ROOT/'tools/marble-fdt.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
 raw=(ROOT/'out/audio-tdm-board-v122-plan/android-runtime.fdt').read_bytes()
 if hashlib.sha256(raw).hexdigest()!='0973dd78214906911189bb3cb8fcab718f3a08aa9d0eca8df763b05347cea973':raise ValueError('runtime FDT changed')
 t=m.parse(raw,1048576,maximum_nodes=16384,maximum_properties=131072);tlmm='/soc/pinctrl@f000000';node='/soc/spf_core_platform/tert_tdm_pinctrl'
 if t[tlmm]['compatible']!=b'qcom,cape-pinctrl\0' or t[tlmm]['reg']!=struct.pack('>II',0xf000000,0x1000000):raise ValueError('actual TLMM provider')
 if t['/soc/spf_core_platform/sound']['qcom,tert-mi2s-gpios']!=t[node]['phandle']:raise ValueError('board pinctrl reference')
 handles={struct.unpack('>I',v['phandle'])[0]:p for p,v in t.items() if 'phandle' in v}
 refs=struct.unpack('>4I',t[node]['pinctrl-0']);pins=[]
 for i,(gpio,function) in enumerate(zip((121,123,122,124),('mi2s2_sck','mi2s2_ws','mi2s2_data0','mi2s2_data1'))):
  p=handles[refs[i]];mux=t[p+'/mux'];cfg=t[p+'/config'];name=('gpio'+str(gpio)+'\0').encode()
  if mux['pins']!=name or cfg['pins']!=name or mux['function']!=(function+'\0').encode() or cfg['drive-strength']!=struct.pack('>I',8):raise ValueError('board pin identity/drive')
  match=re.search(r'\['+str(gpio)+r'\]\s*=\s*PINGROUP\('+str(gpio)+r',\s*(\w+)\s*,',text)
  if not match or match[1]!=function:raise ValueError('actual cape function1')
  bias='bias-pull-down' if i==3 else 'bias-disable'
  if cfg.get(bias)!=b'' or 'output-low' in cfg or 'input-enable' in cfg or (('output-high' in cfg)!=(i<2)):raise ValueError('pin bias/direction')
  value=(1<<2)|(3<<6)|((1<<9) if i<2 else 0)|(1 if i==3 else 0)
  pins.append(dict(gpio=gpio,function=function,mux=1,drive_ma=8,cfg=value,output_latch=1 if i<2 else 0))
 runtime=(ROOT/'xnuloader/marble_audio_pins.h').read_text();values=re.search(r'marble_audio_pins_cfg\[\]\s*=\s*\{([^}]+)\}',runtime)
 if not values or [int(x.strip(),0) for x in values[1].split(',')]!=[x['cfg'] for x in pins]:raise ValueError('compiled pin constants differ from board')
 return dict(passed=True,FDT_sha256=hashlib.sha256(raw).hexdigest(),cape_header_sha256=hashlib.sha256(header).hexdigest(),runtime_header_sha256=hashlib.sha256(runtime.encode()).hexdigest(),TLMM_base=0xf100000,pins=pins,amplifiers_reset=True,scope='offline actual cape mux and electrical binding; no hardware waveform claim')
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--out',required=True,type=Path);a=p.parse_args();r=inspect();a.out.write_text(json.dumps(r,indent=2)+'\n');print('Verified actual cape TDM pin contract')
if __name__=='__main__':main()
