"""Require actual Gen7 table ACKs in addition to the full FW/cleanup proof."""
import argparse,importlib.util,json,re
from pathlib import Path
spec=importlib.util.spec_from_file_location('boot',Path(__file__).with_name('marble-gmu-boot-report.py'))
boot=importlib.util.module_from_spec(spec);spec.loader.exec_module(boot)
def inspect(path):
    result=boot.inspect(path);text=path.read_text(encoding='utf8');errors=result['errors']
    def records(prefix):
        return [dict((k,int(v,0)) for k,v in re.findall(r'(\w+)=(-?0x[0-9a-fA-F]+|-?\d+)',s))
            for s in text.splitlines() if s.startswith(prefix)]
    ack=records('marble-gmu-hfi: ack ')
    if len(ack)!=2 or any(v.get('index')!=i or v.get('returned')!=[0x103b04,0x20a003][i] or
        ((v.get('header',0)>>16)&15)!=1 or ((v.get('header',0)>>8)&255)<3 for i,v in enumerate(ack)):
        errors.append('real typed/sequence-matched Gen7 table ACKs missing')
    expected=[dict(queue=0,read=220,write=220),dict(queue=1,read=8,write=8),dict(queue=2,read=0,write=0)]
    if records('marble-gmu-hfi: queue=')!=expected:errors.append('table queue consumption missing or unexpected packets')
    hfi=records('marble-gmu-hfi: MGPU-MARBLE-GMU-HFI-v1 ')
    wanted=dict(PASS=1,ready=1,commands=2,acks=2,failed_stage=0,ctrl=1,fw_result=0x100,transport=0,error=0,notify=2,pending=0,poisoned=0,skipped=0)
    if len(hfi)!=1 or any(hfi[0].get(k)!=v for k,v in wanted.items()) or not 3<=hfi[0].get('polls',0)<=30003 or hfi[0].get('irq',0)&0x8000:
        errors.append('HFI initialization/table exchange failed')
    if (0,0x16618) not in [(v.get('block'),v.get('offset')) for v in result['registers']]:errors.append('HFI_INIT restoration absent')
    result.update(passed=not errors,hfi=hfi,acks=ack);return result
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('logs',nargs='+',type=Path);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    runs=[inspect(v) for v in a.logs];passed=all(v['passed'] for v in runs)
    a.output.write_text(json.dumps(dict(passed=passed,runs=runs,acceleration_enabled=False,
        limit='HFI table ACKs only; CORE_FW_START/GX/submission/rendering not established'),indent=2)+'\n',encoding='utf8')
    for r in runs:print(r['log'],'PASS' if r['passed'] else 'FAIL',r['errors'])
    raise SystemExit(0 if passed else 1)
if __name__=='__main__':main()
