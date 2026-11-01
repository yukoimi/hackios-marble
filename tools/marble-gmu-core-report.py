"""Verify real CORE_FW_START/low-vote/GX evidence and the enclosing teardown."""
import argparse,importlib.util,json,re
from pathlib import Path
spec=importlib.util.spec_from_file_location('hfi',Path(__file__).with_name('marble-gmu-hfi-report.py'))
hfi=importlib.util.module_from_spec(spec);spec.loader.exec_module(hfi)
def inspect(path,low_vote=False,gx=False,manual_gx=False,gpu_start=False):
    gx=gx or manual_gx or gpu_start
    r=hfi.inspect(path);errors=r['errors'];text=path.read_text(encoding='utf8')
    def records(prefix):
        return [dict((k,int(v,0)) for k,v in re.findall(r'(\w+)=(-?0x[0-9a-fA-F]+|-?\d+)',s))
            for s in text.splitlines() if s.startswith(prefix)]
    def command(v,sent,notify):
        want=dict(PASS=1,commands=1,acks=1,failed_stage=0,fw_result=0x100,transport=0,error=0,
            sent=sent,returned=sent,notify=notify,pending=0,poisoned=0)
        return all(v.get(k)==n for k,n in want.items()) and 1<=v.get('polls',0)<=10001 and \
            ((v.get('ack_header',0)>>16)&15)==1 and ((v.get('ack_header',0)>>8)&255)>=3 and not v.get('irq',0)&0x8000
    core=records('marble-gmu-core: MGPU-MARBLE-GMU-CORE-v1 ')
    if len(core)!=1 or not command(core[0],0x30020e,3):errors.append('CORE_FW_START real ACK absent')
    if low_vote or gx:
        vote=records('marble-gmu-vote: MGPU-MARBLE-GMU-LOW-VOTE-v1 ')
        headers=[0x40041e,0x600321 if gpu_start else 0x500321]
        if len(vote)!=(1 if manual_gx else 2) or any(v.get('slumber')!=i or not command(v,headers[i],i+4+(1 if i and gpu_start else 0)) for i,v in enumerate(vote)):
            errors.append('minimum vote/prepare-slumber real ACKs absent')
        r['low_vote']=vote
    if gx:
        live=records('marble-gmu-gx: MGPU-MARBLE-GMU-GX-v1 ')
        if len(live)!=1 or any(live[0].get(k)!=v for k,v in dict(PASS=1,requested=1,ack=1,powered=1).items()) or \
            not live[0].get('intr',0)&0x80000000 or live[0].get('gfx',3)&3 or not live[0].get('gdscr',0)&0x80000000:
            errors.append('real GX power/clock/OOB evidence absent')
        saved=records('marble-gmu-gx: register ')
        if len(saved)!=5 or [v.get('offset') for v in saved]!=[0x905c,0x9504,0x9058,0x9358,0x958c] or any(v.get('before')!=v.get('after') for v in saved):errors.append('GX regulator/reset/clamp restoration differs')
        stopped=records('marble-gmu-gx: stopped ')
        if len(stopped)!=1 or any(stopped[0].get(k)!=v for k,v in dict(rc=0,halted=1,collapsed=1,restored=1,phase=3).items()):errors.append('GX stopped state absent')
        if manual_gx:
            manual=records('marble-gmu-gx: force_stop_test=')
            if manual!=[dict(force_stop_test=1,slumber_not_sent=1)] or len(stopped)!=1 or stopped[0].get('halt_ack')!=1:
                errors.append('forced GX stop/drain evidence absent')
        elif records('marble-gmu-gx: force_stop_test='):
            errors.append('manual GX diagnostic cannot satisfy normal slumber mode')
        positions=[text.find(s) for s in ['marble-gmu-gx: MGPU-MARBLE-GMU-GX-v1',
            'marble-gmu-step: before-write block=0 offset=0x16000 value=0x1',
            'marble-gmu-step: before-write block=1 offset=0xf114 value=0x2',
            'marble-gmu-gx: stopped ','marble-gmu-boot: MGPU-MARBLE-GMU-BOOT-v1']]
        if -1 in positions or positions!=sorted(positions):errors.append('GX/CM3 reset/CX drain/release order differs')
        r.update(gx=live,gx_saved=saved,gx_stopped=stopped)
    start=records('marble-gmu-start: MGPU-MARBLE-GMU-START-v1 ')
    if gpu_start:
        if len(start)!=1 or not command(start[0],0x50010a,5) or start[0].get('held')!=1:
            errors.append('GPU START real ACK under held OOB force absent')
        markers=['marble-gmu-gx: MGPU-MARBLE-GMU-GX-v1','marble-gmu-start: MGPU-MARBLE-GMU-START-v1','marble-gmu-vote: MGPU-MARBLE-GMU-LOW-VOTE-v1 slumber=1']
        positions=[text.find(s) for s in markers]
        if -1 in positions or positions!=sorted(positions):errors.append('GX/START/slumber order differs')
    elif start:
        errors.append('START evidence requires explicit gpu-start mode')
    r['gpu_start']=start
    r.update(passed=not errors,core=core);return r
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('logs',nargs='+',type=Path);p.add_argument('--output',required=True,type=Path);p.add_argument('--low-vote',action='store_true');p.add_argument('--gx',action='store_true');p.add_argument('--manual-gx',action='store_true');p.add_argument('--gpu-start',action='store_true');a=p.parse_args()
    if a.manual_gx and a.gpu_start:p.error('manual-gx and gpu-start are distinct diagnostics')
    runs=[inspect(v,a.low_vote,a.gx,a.manual_gx,a.gpu_start) for v in a.logs];passed=all(v['passed'] for v in runs)
    a.output.write_text(json.dumps(dict(passed=passed,acceleration_enabled=False,
        limits='GX power/clock status is not GPU command execution, measured frequency or accelerated iOS rendering',runs=runs),indent=2)+'\n',encoding='utf8')
    for r in runs:print(r['log'],'PASS' if r['passed'] else 'FAIL',r['errors'])
    raise SystemExit(0 if passed else 1)
if __name__=='__main__':main()
