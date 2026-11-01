"""Bind minimum QUP2 BCM keepalive to the actual board and pinned drivers."""
import argparse,hashlib,importlib.util,json,re,struct
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def inspect():
 base=ROOT/'out/audio-i2c-vendor-v159-audit';manifest=json.loads((base/'sources.json').read_text())
 if manifest['commit']!='48952ed36228217531482b39d5bef13e7fd808ec':raise ValueError('board source commit')
 sources={}
 for r in manifest['files']:
  raw=(base/r['path']).read_bytes();blob=hashlib.sha1(b'blob '+str(len(raw)).encode()+b'\0'+raw).hexdigest()
  if blob!=r['git_blob'] or hashlib.sha256(raw).hexdigest()!=r['sha256']:raise ValueError('source identity '+r['path'])
  sources[r['path']]=r['sha256']
 raw=(ROOT/'out/audio-tdm-board-v122-plan/android-runtime.fdt').read_bytes()
 if hashlib.sha256(raw).hexdigest()!='0973dd78214906911189bb3cb8fcab718f3a08aa9d0eca8df763b05347cea973':raise ValueError('actual board FDT')
 s=importlib.util.spec_from_file_location('qup_fdt',ROOT/'tools/marble-fdt.py');m=importlib.util.module_from_spec(s);s.loader.exec_module(m)
 t=m.parse(raw,1048576,maximum_nodes=16384,maximum_properties=131072)
 wrapper=t['/soc/qcom,qupv3_2_geni_se@8c0000'];i2c=t['/soc/i2c@880000']
 if i2c['compatible']!=b'qcom,i2c-geni\0' or i2c['qcom,wrapper-core']!=wrapper['phandle'] or wrapper['compatible']!=b'qcom,qupv3-geni-se\0':raise ValueError('I2C vendor driver/wrapper binding')
 if struct.unpack('>3I',i2c['interrupts'])!=(0,373,4):raise ValueError('I2C SPI373 level-high / GIC INTID405 binding')
 handles={struct.unpack('>I',v['phandle'])[0]:p for p,v in t.items() if 'phandle' in v}
 core=struct.unpack('>4I',wrapper['interconnects'][:16])
 if core[0]!=core[2] or core[1]!=42 or core[3]!=577 or t[handles[core[0]]]['compatible']!=b'qcom,waipio-clk_virt\0' or wrapper['interconnect-names'].split(b'\0')[0]!=b'qup-core':raise ValueError('actual QUP2 core interconnect')
 waipio_raw=(ROOT/'out/adsp-dependency-source-v1-plan/drivers/interconnect/qcom/waipio.c').read_bytes()
 if hashlib.sha256(waipio_raw).hexdigest()!='82d5425eec718e5cb6587dc1d8563fe4e48ed645e0840bf7dcf83f27b16191dc':raise ValueError('pinned waipio source')
 sources['drivers/interconnect/qcom/waipio.c']=hashlib.sha256(waipio_raw).hexdigest();waipio=waipio_raw.decode()
 bcm=re.search(r'static struct qcom_icc_bcm bcm_qup2\s*=\s*\{(.*?)\};',waipio,re.S)
 if not bcm or not all(x in bcm[1] for x in ('.name = "QUP2"','.keepalive_early = true','.vote_scale = 1','&qup2_core_slave')):raise ValueError('QUP2 source BCM definition')
 # Both actual vendor files are required; mainline i2c-qcom-geni is not this
 # board's compatible. The resource driver requests bandwidth before clocks.
 vendor=(base/'drivers/i2c/busses/i2c-msm-geni.c').read_text();rsc=(base/'drivers/platform/msm/msm-geni-se.c').read_text()
 if '.compatible = "qcom,i2c-geni"' not in vendor or 'se_geni_resources_on(&gi2c->i2c_rsc)' not in vendor:raise ValueError('actual I2C driver resource path')
 start=rsc.index('int se_geni_clks_on(');end=rsc.index('EXPORT_SYMBOL(se_geni_clks_on)',start)
 if not rsc.index('geni_se_add_ab_ib',start,end)<rsc.index('clk_prepare_enable',start,end):raise ValueError('bus vote before clocks')
 voter=(base/'drivers/interconnect/qcom/bcm-voter.c').read_text()
 if not re.search(r'else if.*?vote_x\[QCOM_ICC_BUCKET_AMC\] = 1;.*?vote_y\[QCOM_ICC_BUCKET_WAKE\] = 1;',voter,re.S):raise ValueError('post-init minimum keepalive')
 h=(base/'include/soc/qcom/tcs.h').read_text()
 shifts={k:int(v) for k,v in re.findall(r'#define\s+BCM_TCS_CMD_(COMMIT|VALID|VOTE_X|VOTE_Y)_SHFT\s+(\d+)',h)}
 if shifts!={'COMMIT':30,'VALID':29,'VOTE_X':14,'VOTE_Y':0}:raise ValueError('BCM command layout')
 command=(1<<30)|(1<<29)|(1<<14)|1
 runtime=(ROOT/'xnuloader/marble_qup_keepalive.h').read_text()
 if f'#define MARBLE_QUP_KEEPALIVE_DATA 0x{command:x}u' not in runtime:raise ValueError('compiled minimum vote')
 return dict(passed=True,FDT_sha256=hashlib.sha256(raw).hexdigest(),sources=sources,wrapper='/soc/qcom,qupv3_2_geni_se@8c0000',
  source_BCM='QUP2',minimum_x=1,minimum_y=1,command=command,sets=[2,0],sleep_votes=0,zero_votes=0,
  initial_16000_votes_used=False,CPU_GPU_frequency_requests=0,voltage_requests=0,audio_acceptance=False)
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--out',required=True,type=Path);a=p.parse_args();r=inspect();a.out.write_text(json.dumps(r,indent=2)+'\n');print('Verified actual vendor QUP2 resource path and minimum ACTIVE/WAKE vote')
if __name__=='__main__':main()
