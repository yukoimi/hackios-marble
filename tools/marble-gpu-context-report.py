"""Require real bank1/ECATS and enclosing HFI/GX teardown evidence."""
import argparse,importlib.util,json,re
from pathlib import Path
spec=importlib.util.spec_from_file_location('core',Path(__file__).with_name('marble-gmu-core-report.py'))
core=importlib.util.module_from_spec(spec);spec.loader.exec_module(core)

def map_records(text):
    """Read complete original console records, excluding timestamped IOLog copies."""
    prefix='marble-gpu-live-map: map='
    pattern=r'marble-gpu-live-map: map=([01]) ranges=(\d+) leaves=(\d+) generation=(\d+) published=(\d+) tlb_removed=([01]) PASS=(\d+)'
    fields=('map','ranges','leaves','generation','published','tlb_removed','PASS')
    result=[]
    for line in text.splitlines():
        if re.match(r'^\[\s*\d+\.\d+\]: ',line) or prefix not in line:continue
        if line.count(prefix)!=1:raise ValueError('multiple map records on one console line')
        match=re.fullmatch(pattern,line[line.index(prefix):])
        if not match:raise ValueError('incomplete live BO map record')
        result.append(dict(zip(fields,map(int,match.groups()))))
    return result
def inspect(path,lpac=False,live=False):
    r=core.inspect(path,gpu_start=True);errors=r['errors'];text=path.read_text(encoding='utf8')
    def records(prefix):
        return [dict((k,int(v,0)) for k,v in re.findall(r'(\w+)=(-?0x[0-9a-fA-F]+|-?\d+)',s))
            for s in text.splitlines() if s.startswith('marble-gpu-context: '+prefix)]
    def one(prefix,want):
        a=records(prefix)
        if len(a)!=1 or any(a[0].get(k)!=v for k,v in want.items()):errors.append('GPU context '+prefix+' absent/invalid')
        return a[0] if len(a)==1 else {}
    alloc=one('inactive ',dict(PASS=1,pages=5,leaves=48,SQE_bytes=74672,SQE_skip=4))
    pa=alloc.get('PA',0);tp=alloc.get('table_PA',0)
    if not pa or not tp or (pa|tp)&0x3fff or pa+0x30000>1<<36 or tp+0x8000>1<<36 or (pa<tp+0x8000 and tp<pa+0x30000):errors.append('GPU owned RAM layout invalid')
    one('program ',dict(status=0,phase=5,enabled=1,retain=1,bank=1,asid=0x43))
    attached=dict(status=0,streams=4 if lpac else 2,bank=1,SID0=0,SID1=0x400,tables=5,leaves=48)
    if lpac:attached.update(LPAC0=1,LPAC1=0x401)
    if live:attached.update(tables=258,leaves=0)
    one('attach ',attached)
    ecats=records('ECATS ')
    if len(ecats)!=5:errors.append('GPU hardware translations absent/duplicate')
    for i,v in enumerate(ecats):
        want=pa+(0 if i==0 else 0x20000+(i-1)*0x4000)
        if any(v.get(k)!=n for k,n in dict(index=i,sid=0,iova=0x100000*(i+1),PA=want,expected=want,status=0,restored=1,retain=0,PASS=1).items()) or v.get('par',1)&1 or (v.get('par',0)>>12)&0xfffffffff!=want:
            errors.append('GPU ECATS translation differs: '+str(i))
    one('ready ',dict(PASS=1,bank=1,asid=0x43,routes=4 if lpac else 2,ranges=5,writes=24 if live else 22 if lpac else 18))
    # The initial observed batch and all 64 unmaps write TLBIASID/TLBSYNC.
    # Later maps publish previously invalid leaves without invalidating TLBs.
    live_syncs=65
    if live=='service':
        updates=map_records(text)
        live_syncs=sum(v.get('map')==0 or (v.get('generation',0)-1)%64==0 for v in updates)
    one('stopped ',dict(PASS=1,reset=1,bank=1,roots_zero=1,routes_faulted=4 if lpac else 2,tlb_sync=1,irq_fault=0,writes=44+2*live_syncs if live else 44 if lpac else 32))
    markers=['marble-gpu-context: ready ','marble-gmu-start: MGPU-MARBLE-GMU-START-v1','marble-gmu-gx: stopped ','marble-gpu-context: stopped ','marble-gmu-boot: MGPU-MARBLE-GMU-BOOT-v1']
    at=[text.find(s) for s in markers]
    if -1 in at or at!=sorted(at):errors.append('GPU bank/GX stop/root release order differs')
    if 'HEALTH_FAIL' in text or 'marble-gmu-health: FAIL' in text:errors.append('hardware health failure present')
    r.update(passed=not errors,gpu_ecats=ecats);return r
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('logs',nargs='+',type=Path);p.add_argument('--output',required=True,type=Path);p.add_argument('--lpac',action='store_true');a=p.parse_args()
    runs=[inspect(v,a.lpac) for v in a.logs];passed=all(v['passed'] for v in runs)
    a.output.write_text(json.dumps(dict(passed=passed,acceleration_enabled=False,limits='ECATS is address translation, not GPU CP or rendering',runs=runs),indent=2)+'\n',encoding='utf8')
    for r in runs:print(r['log'],'PASS' if r['passed'] else 'FAIL',r['errors'])
    raise SystemExit(0 if passed else 1)
if __name__=='__main__':main()
