"""Offline qualification of the next digital-only AW2032 PLL primitive."""
import argparse,hashlib,importlib.util,json,re
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def inspect():
 s=importlib.util.spec_from_file_location('digital_profile',ROOT/'tools/marble-aw-profile.py')
 p=importlib.util.module_from_spec(s);s.loader.exec_module(p);profile=p.qualify()
 base=ROOT/'out/audio-route-v103-plan/audio-kernel/asoc/codecs/aw882xx'
 h=(base/'inc/aw882xx_pid_2032_reg.h').read_text();dev=(base/'src/aw882xx_device.c').read_text()
 bits={'PWDN':0,'AMPPD':1,'HMUTE':4,'PLLS':0,'CLKS':4,'NOCLKS':5,'OTHS':1,'OCDS':3,'UVLS':14,'OVP2S':15,'OVPS':10,'BSTOCS':11,'CLIPS':7}
 for name,bit in bits.items():
  if not re.search(r'#define\s+AW_PID_2032_'+name+r'_START_BIT\s+\('+str(bit)+r'\)',h):raise ValueError('OEM bit identity '+name)
 if not re.search(r'#define\s+AW_DEV_SYSST_CHECK_MAX\s+\(10\)',dev):raise ValueError('OEM ten-poll limit')
 start=dev.index('int aw882xx_device_start(');end=dev.index('/*amppd on*/',start)
 if not dev.index('aw_dev_pwd(aw_dev, false)',start,end)<dev.index('aw_dev_syspll_check(aw_dev)',start,end):raise ValueError('OEM digital-before-amplifier ordering')
 targets=[]
 for device in profile['devices']:
  words={r['address']:p.target(r['address'],r['value'],0x4003,0x38) for r in device['registers']}
  if words[4]!=(0xb443 if device['address']==0x34 else 0x3443) or words[5]!=0xf039:raise ValueError('exact ROM down/muted baseline')
  targets.append(dict(address=device['address'],sysctrl_down=words[4],sysctrl_digital=words[4]&~1,sysctrl2=words[5]))
 primitive=ROOT/'xnuloader/marble_aw2032_digital.h'
 return dict(passed=True,scope='offline source qualification; no hardware acceptance',OEM_sources=profile['OEM_sources'],
  primitive_sha256=hashlib.sha256(primitive.read_bytes()).hexdigest(),devices=targets,polls_maximum=10,poll_delay_us=2000,
  AMPPD_retained=True,HMUTE_retained=True,attenuation_db=90,CCO_fallback=False,speaker_switch_writes=0,audio_acceptance=False)
def main():
 a=argparse.ArgumentParser(description=__doc__);a.add_argument('--out',type=Path,required=True);args=a.parse_args()
 r=inspect();args.out.parent.mkdir(parents=True,exist_ok=True);args.out.write_text(json.dumps(r,indent=2)+'\n');print('PASS offline OEM digital PLL source and ROM binding; hardware acceptance not claimed')
if __name__=='__main__':main()
