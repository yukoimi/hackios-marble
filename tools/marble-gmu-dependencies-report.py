#!/usr/bin/env python3
"""Verify retained ARC/BCM request sufficiency against original-source table."""
import argparse,importlib.util,json,re
from pathlib import Path
spec=importlib.util.spec_from_file_location('rpmh',Path(__file__).with_name('marble-gmu-rpmh-report.py'))
rpmh=importlib.util.module_from_spec(spec);spec.loader.exec_module(rpmh)
def inspect(path,oracle):
    r=rpmh.inspect(path,oracle,stages=(16,));errors=r['errors']
    lines=path.read_bytes().decode('utf8','replace').splitlines()
    def select(prefix):
        out=[]
        for line in lines:
            if line.startswith(prefix):
                record=rpmh.clocks.votes.preflight.fields(line[len(prefix):]);name=re.search(r'\bname=(\w+)',line)
                if name:record['name']=name[1]
                out.append(record)
        return out
    regs=select('marble-gmu-dependencies: register ')
    if len(regs)!=7 or [v.get('offset') for v in regs]!=[0,0x10,0x14,0x1c,0x30,0x34,0x38] or any(v.get('before')!=v.get('after') for v in regs):errors.append('BCM read TCS configuration not restored')
    responses=select('marble-gmu-dependencies: response ');names=['CN0','SH0','MC0','ACV']
    resources={v['name']:v for v in r.get('resources',[])}
    if len(responses)!=8:errors.append('BCM response count differs')
    expected_min=[oracle['tables']['bandwidth'][i] for i in [17,39,40,41]]
    for i,v in enumerate(responses):
        if i>=8 or v.get('index')!=i or v.get('name')!=names[i//2] or v.get('address')!=resources.get(names[i//2],{}).get('address') or v.get('status',0)&0x10100!=0x10100:errors.append('BCM response identity/status differs');continue
        if i%2 and v['data']!=responses[i-1]['data']:errors.append('BCM response not stable')
        d=v['data'];minimum=expected_min[i//2]
        covers=bool(d&0x20000000) and (((d&minimum&0x3fff)==(minimum&0x3fff)) if i//2==3 else ((d&0x3fff)>=(minimum&0x3fff) and ((d>>14)&0x3fff)>=((minimum>>14)&0x3fff)))
        if not covers:errors.append('BCM retained request below source minimum')
    if select('marble-gmu-dependencies: minimum ')!=[dict(name=n,data=d,covers=1) for n,d in zip(names,expected_min)]:errors.append('minimum table/evidence differs')
    levels=[]
    for i,name in enumerate(['cx.lvl','mx.lvl']):
        data=r.get('rpmh',{}).get('responses',[])
        index=data[i*2]['data'] if len(data)==8 else 999
        table=resources.get(name,{}).get('levels',[])
        level=table[index] if 0<=index<len(table) else 0;levels.append(level)
        if level<64:errors.append('ARC dependency below required level')
    expected=dict(PASS=1,cx_level=levels[0],mx_level=levels[1],adequate=1,reads=8,completed=8,restored=1,retain=0,phase=5,hardware_votes=0,reset_released=0)
    if select('marble-gmu-dependencies: MGPU-MARBLE-GMU-DEPENDENCIES-v1 ')!=[expected]:errors.append('dependency transaction incomplete')
    r['dependencies']=dict(registers=regs,responses=responses,minimum=expected_min);r['passed']=not errors
    return r
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('logs',type=Path,nargs='+');p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    oracle=rpmh.clocks.votes.load_oracle(rpmh.clocks.votes.ROOT/'out/marble-gmu-votes-oracle.json');runs=[inspect(path,oracle) for path in a.logs];passed=all(r['passed'] for r in runs)
    a.output.write_text(json.dumps(dict(passed=passed,runs=runs,acceleration_enabled=False,limits=['Firmware-retained APSS votes read only; no voltage measurement or GMU execution.']),indent=2)+'\n')
    for r in runs:
        print(r['log'],'PASS' if r['passed'] else 'FAIL')
        for e in r['errors']:print(e)
    raise SystemExit(0 if passed else 1)
if __name__=='__main__':main()
