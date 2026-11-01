"""Require real CP DMA and a complete enclosing GPU shutdown; no render claim."""
import argparse,importlib.util,json,re
from pathlib import Path
spec=importlib.util.spec_from_file_location('context',Path(__file__).with_name('marble-gpu-context-report.py'))
context=importlib.util.module_from_spec(spec);spec.loader.exec_module(context)

def inspect(path,extra_writes=0,live=False):
    r=context.inspect(path,lpac=True,live=live);errors=r['errors'];text=path.read_text(encoding='utf8')
    def one(prefix,want):
        lines=[s for s in text.splitlines() if s.startswith(prefix)]
        vals=[dict((k,int(v,0)) for k,v in re.findall(r'(\w+)=(-?0x[0-9a-fA-F]+|-?\d+)',s)) for s in lines]
        if len(vals)!=1 or any(vals[0].get(k)!=v for k,v in want.items()):
            errors.append(prefix+' missing/duplicate/invalid')
        return vals[0] if len(vals)==1 else {}
    cp=one('marble-gpu-cp: MGPU-MARBLE-CP-v2 ',dict(PASS=1,configured=1,kicked=1,completed=1,
        words=21,rptr=21,wptr=21,shadow=21,fence=1,marker=0x4d475031,expected=0x4d475031,
        scratch=0x4d475031,status3=0,irq=0,fault=0,halt_requested=1,phase=3))
    if not 1<=cp.get('polls',0)<=10001 or cp.get('status',2)&~1:
        errors.append('CP completion not idle or poll bound invalid')
    if (cp.get('halt_observed'),cp.get('halt')) not in ((0,1),(1,3)):
        errors.append('SQE halt sample differs; request alone is not drain proof')
    bus=one('marble-gpu-cp: CX-bus-restore ',dict(PASS=1,changed=1,restored=1))
    # Require both the actual release order and matching posted write pairs.
    markers=['marble-gpu-cp: after-write reg=0x808\n',
        'marble-gpu-cp: before-write reg=0x807 value=0x15',
        'marble-gpu-cp: MGPU-MARBLE-CP-v2 ',
        'marble-gmu-vote: MGPU-MARBLE-GMU-LOW-VOTE-v1 slumber=1',
        'marble-gmu-gx: stopped ','marble-gpu-cp: CX-bus-restore ',
        'marble-gpu-context: stopped ','marble-gmu-boot: MGPU-MARBLE-GMU-BOOT-v1']
    positions=[text.find(s) for s in markers]
    if -1 in positions or positions!=sorted(positions):errors.append('CP kick/slumber/GX/bus/root release order differs')
    pending=None;writes=0
    for line in text.splitlines():
        if line.startswith('marble-gpu-cp: before-write '):
            m=re.fullmatch(r'marble-gpu-cp: before-write reg=(0x[0-9a-f]+|0) value=(0x[0-9a-f]+|0)',line)
            if not m or pending is not None:errors.append('unpaired/invalid CP write')
            pending=int(m[1],0) if m else -1
        elif line.startswith('marble-gpu-cp: after-write '):
            m=re.fullmatch(r'marble-gpu-cp: after-write reg=(0x[0-9a-f]+|0)',line)
            if not m or pending!=int(m[1],0):errors.append('CP posted write boundary differs')
            pending=None;writes+=1
    if pending is not None or writes!=94+extra_writes:errors.append('CP configuration/kick/stop/bus restore write count differs')
    r.update(passed=not errors,cp=cp,bus=bus,paired_writes=writes);return r

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('logs',type=Path,nargs='+');p.add_argument('--output',required=True,type=Path);a=p.parse_args()
    runs=[inspect(v) for v in a.logs];passed=all(r['passed'] for r in runs)
    a.output.write_text(json.dumps(dict(passed=passed,cp_dma_verified=passed,acceleration_enabled=False,
        limits='CP register, memory-write and cache-clean fence completion; no shader, rasterization or iOS GPU composition proof',runs=runs),indent=2)+'\n',encoding='utf8')
    for r in runs:print(r['log'],'PASS' if r['passed'] else 'FAIL',r['errors'])
    raise SystemExit(0 if passed else 1)
if __name__=='__main__':main()
