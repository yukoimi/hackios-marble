#!/usr/bin/env python3
"""Check real RPMh ARC read completions, repeatability and TCS restoration."""
import argparse
import importlib.util
import json
import re
from pathlib import Path
spec=importlib.util.spec_from_file_location('clocks',Path(__file__).with_name('marble-gmu-clocks-report.py'))
clocks=importlib.util.module_from_spec(spec);spec.loader.exec_module(clocks)
def inspect(path,oracle,stages=(15,)):
    result=clocks.inspect(path,oracle,stages=stages);errors=result['errors']
    lines=path.read_bytes().decode('utf8','replace').splitlines()
    def select(prefix):return [clocks.votes.preflight.fields(s[len(prefix):]) for s in lines if s.startswith(prefix)]
    regs=select('marble-gmu-rpmh: register ')
    if len(regs)!=7 or [r.get('offset') for r in regs]!=[0,0x10,0x14,0x1c,0x30,0x34,0x38]:errors.append('TCS register inventory differs')
    if any(set(r)!={'offset','before','after'} or r.get('before')!=r.get('after') for r in regs):errors.append('TCS configuration not restored')
    responses=select('marble-gmu-rpmh: response ')
    response_lines=[s for s in lines if s.startswith('marble-gmu-rpmh: response ')]
    for r,line in zip(responses,response_lines):
        match=re.search(r'\bname=([a-z]+\.lvl)(?: |$)',line)
        r['name']=match[1] if match else None
    if len(responses)!=8:errors.append('missing or duplicate responses')
    names=['cx.lvl','mx.lvl','gfx.lvl','mxc.lvl']
    resources={r['name']:r for r in result.get('resources',[])}
    for i,r in enumerate(responses):
        if i>=8 or r.get('index')!=i or r.get('name')!=names[i//2] or r.get('address')!=resources.get(names[i//2],{}).get('address') or (r.get('status',0)&0x10100)!=0x10100:
            errors.append('ARC response identity/status differs');continue
        if i%2 and r['data']!=responses[i-1]['data']:errors.append('ARC reads unstable')
    expected=dict(PASS=1,reads=8,completed=8,changed=1,restored=1,retain=0,refused=0,phase=5,rail_writes=0,reset_released=0)
    if select('marble-gmu-rpmh: MGPU-MARBLE-GMU-RPMH-v1 ')!=[expected]:errors.append('RPMh transaction incomplete')
    result['rpmh']=dict(registers=regs,responses=responses)
    result['passed']=not errors
    return result
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('logs',type=Path,nargs='+');p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    oracle=clocks.votes.load_oracle(clocks.votes.ROOT/'out/marble-gmu-votes-oracle.json')
    runs=[inspect(path,oracle) for path in a.logs];passed=all(r['passed'] for r in runs)
    a.output.write_text(json.dumps(dict(passed=passed,runs=runs,acceleration_enabled=False,limits=['ARC responses are not measured rail voltages.','No rail WRITE packet issued; GMU held reset.']),indent=2)+'\n')
    for r in runs:
        print(r['log'],'PASS' if r['passed'] else 'FAIL')
        for e in r['errors']:print(e)
    raise SystemExit(0 if passed else 1)
if __name__=='__main__':main()
