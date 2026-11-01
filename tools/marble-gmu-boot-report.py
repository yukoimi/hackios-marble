#!/usr/bin/env python3
"""Require real FW_INIT, stopped bus, full TCM restoration and SMMU release."""
import argparse,hashlib,json,re
from pathlib import Path

REGS=[(0,0x163c4),(0,0x163c0),(2,0xe4),(0,0x16070),(0,0x16004),
      (0,0x16614),(0,0x16610),(0,0x26c44),(0,0x160b4),(1,0xf114),
      (0,0x16738),(0,0x1673c),(0,0x16734),(0,0x1503c),(0,0x15000),
      (0,0x15004),(0,0x1664c),(0,0x26c18),(0,0x26c40)]

def inspect(path,diagnostic=False):
    raw=path.read_bytes();text=raw.decode('utf8','replace');lines=text.splitlines();errors=[]
    def select(prefix):
        # Loader mirror is unprefixed; console repeats timestamped records.
        return [dict((k,int(v,0)) for k,v in re.findall(r'(\w+)=(-?0x[0-9a-fA-F]+|-?\d+)',s))
                for s in lines if s.startswith(prefix)]
    regs=select('marble-gmu-boot: register ')
    expected_regs=REGS+([(0,0x16618)] if len(regs)==20 else [])
    if len(regs)!=len(expected_regs) or [(v.get('block'),v.get('offset')) for v in regs]!=expected_regs or any(v.get('before')!=v.get('after') for v in regs):errors.append('startup register inventory/restoration differs')
    boot=select('marble-gmu-boot: MGPU-MARBLE-GMU-BOOT-v1 ')
    expected=dict(PASS=1,configured=1,reset_released=1,initialized=1,fw_result=0x100,stopped=1,ack=2,restored=1,retain=0,refused=0,phase=5)
    if diagnostic:expected.update(PASS=0,initialized=0,fw_result=0)
    if len(boot)==1 and boot[0].get('ack',0)&2:expected['ack']=boot[0]['ack']
    if boot!=[expected]:errors.append('fresh firmware completion or stopped/restored state missing')
    if diagnostic:
        fetch=diagnostic=='fetch'
        dma=select('marble-gmu-fetch: MGPU-MARBLE-GMU-FETCH-v1 ' if fetch else 'marble-gmu-dma: MGPU-MARBLE-GMU-DMA-v1 ')
        if len(dma)!=1 or any(dma[0].get(k)!=v for k,v in dict(PASS=1,progress=0x4d475046 if fetch else 0x4d475052,read=0xbd1fb51f,expected=0xbd1fb51f,written=0xdec0de01,done=0x4d475044,vendor_firmware=0).items()):errors.append('real M3 DMA read/write/fetch evidence missing')
        if select('marble-gmu-dma: CPU_after_invalidate=')!=[dict(CPU_after_invalidate=1,expected=0xdec0de01)]:errors.append('CPU observation of GMU DMA write missing')
    if diagnostic=='fetch' or not diagnostic:
        if 'exact SID5+SID405 quarantined and both SMRs invalid' not in text or not all(s in text for s in ['write32 offset=0x804 value=0x80000405','write32 offset=0xc04 value=0','write32 offset=0x804 value=0']):errors.append('DT dual exact stream lease not verified')
    rsc=select('marble-gmu-boot: RSC ')
    if len(rsc)!=4 or any(v.get('index')!=i or not v.get('before',0)&1 or not v.get('after',0)&1 for i,v in enumerate(rsc)):errors.append('RSC transactions not all idle')
    if select('marble-gmu-boot: IRQ ')!=[dict(index=i,before=0,after=0) for i in range(2)]:errors.append('IRQ cleanup not proven')
    tcm=select('marble-gmu-boot: TCM ')
    if len(tcm)!=1 or any(tcm[0].get(k)!=v for k,v in dict(PASS=1,stable=1,loaded=1,restored=1,retain=0,phase=7).items()) or tcm[0].get('reads')!=40960 or not 135<=tcm[0].get('writes',0)<=8327:errors.append('full TCM load/restore not proven')
    clock=select('marble-gmu-boot: clock_lease ')
    if len(clock)!=1 or any(clock[0].get(k)!=v for k,v in dict(PASS=1,restored=1,retain=0).items()) or not 190000000<=clock[0].get('hz_gmu',0)<=210000000 or not 142500000<=clock[0].get('hz_hub',0)<=157500000:errors.append('held clock lease or restoration missing')
    contexts=select('marble-smmu: context result ')
    if len(contexts)!=1 or any(contexts[0].get(k)!=v for k,v in dict(passed=1,translation=1,released=1,fault_irq=0,stage=17).items()):errors.append('active SID5/CB0 lease not released cleanly')
    pos=[text.find(s) for s in ['marble-ecats: stream attach status=0','marble-gmu-boot: MGPU-MARBLE-GMU-BOOT-v1','marble-gmu-boot: TCM ','marble-gmu-boot: clock_lease ','marble-ecats: SID5 quarantined','marble-smmu: context result ']]
    if -1 in pos or pos!=sorted(pos):errors.append('master/TCM/clocks/SMMU lifecycle order invalid')
    if 'CX result powered=1 clocks=1 observed=1 restored=1 skipped=0' not in text:errors.append('CX cleanup missing')
    if '=== 36 checks, 0 failed ===' not in text:errors.append('userspace smoke completion missing')
    stress_four=re.findall(r'^marble-cpu-stress: PASS activecpu=4 threads=4 wall_ms=\d+ cpu_ms=\d+ timer_drift_us=\d+ errors=0; frequency unchanged; no temperature sensor$',text,re.M)
    if ('hw.activecpu 4' not in text and len(stress_four)!=1) or any(f'returned secondary {core} PSCI state 1 (OFF=1)' not in text for core in (600,500,400)):errors.append('four-core operation or shutdown missing')
    if 'panic(cpu' in text or 'Exception class' in text:errors.append('kernel panic/exception')
    return dict(log=str(path),sha256=hashlib.sha256(raw).hexdigest(),passed=not errors,errors=errors,boot=boot,tcm=tcm,clock=clock,registers=regs,rsc=rsc)

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('logs',type=Path,nargs='+');p.add_argument('--output',type=Path,required=True);g=p.add_mutually_exclusive_group();g.add_argument('--diagnostic-dma',action='store_true');g.add_argument('--diagnostic-fetch',action='store_true');a=p.parse_args()
    mode='fetch' if a.diagnostic_fetch else a.diagnostic_dma
    runs=[inspect(path,mode) for path in a.logs];passed=all(r['passed'] for r in runs)
    a.output.write_text(json.dumps(dict(passed=passed,runs=runs,acceleration_enabled=False,vendor_firmware_initialized=passed and not mode,limits=['Local test DMA is not vendor FW_INIT; FW_INIT alone is not HFI, GPU submission or accelerated iOS rendering.']),indent=2)+'\n')
    for r in runs:
        print(r['log'],'PASS' if r['passed'] else 'FAIL',*r['errors'],sep='\n')
    raise SystemExit(0 if passed else 1)
if __name__=='__main__':main()
