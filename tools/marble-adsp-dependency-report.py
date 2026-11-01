"""Require simultaneous RUN/XO/LPI/bandwidth and bounded release; no DSP acceptance."""
import argparse,importlib.util,json,re
from pathlib import Path
def load(name,file):
 s=importlib.util.spec_from_file_location(name,Path(__file__).with_name(file));m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m
run=load('dependency_run','marble-icb-run-report.py');xo=load('dependency_xo','marble-xo-rpmh-report.py')
bw=load('dependency_bw','marble-icb-bandwidth-report.py');npa=load('dependency_npa','marble-npa-report.py')
def inspect(text):
 errors=[]
 markers=['marble-adsp-dependency: begin contract=RUN-XO-LPI-bandwidth-reset-v1 DSP_start=0\n',
  'marble-adsp-dependency: phase=lpi configured_level=256\n',
  'marble-adsp-dependency: phase=xo LPI_clients=2 LPI_level=256\n',
  'marble-adsp-dependency: phase=bandwidth baseline=RUN_XO_LPI_held\n']
 pos=[text.find(s) for s in markers]
 if min(pos)<0 or pos!=sorted(pos) or any(text.count(s)!=1 for s in markers):errors.append('simultaneous dependency phase boundary missing')
 first=text[pos[0]:pos[1]] if min(pos[:2])>=0 else ''
 xotext=text[pos[2]:pos[3]] if min(pos[2:])>=0 else ''
 bwtext=text[pos[3]:] if pos[3]>=0 else ''
 metadata=bw.power.inspect_dependencies(first)
 reports={'run':run.inspect(first),'xo':xo.inspect(xotext),'bandwidth':bw.inspect(bwtext,True,2,metadata),
  'lpi':npa._inspect_native_release(text,15000000)}
 for k,v in reports.items():errors.extend(k+': '+e for e in v['errors'])
 def rows(prefix):return [dict(re.findall(r'(\w+)=([^\s]+)',l)) for l in text.splitlines() if l.startswith(prefix)]
 def exact(r,**v):return all(r.get(k)==str(x) for k,x in v.items())
 simultaneous=rows('marble-adsp-dependency: simultaneous ');final=rows('marble-adsp-dependency: complete ')
 if len(simultaneous)!=1 or not exact(simultaneous[0],PASS=1,XO_ack=1,bandwidth_ib=1000000,LPI_clients=2,LPI_level=256,hold_us=10000,DSP_start=0):errors.append('live simultaneous lease observation missing')
 if len(final)!=1 or not exact(final[0],PASS=1,LPI_released=1,LPI_active=0,XO_retained=1,thermal_ok=1,ICB_RUN_retained=1,watchdog_armed=1,cold_reset_required=1,original_hardware_restored=0,DSP_start=0,storage_access=0,audio_acceptance=0):errors.append('dependency release/retained-XO boundary missing')
 held=rows('marble-npa-held: returned ')
 if len(held)!=1 or not exact(held[0],outcome=1,release_allowed=1,active=2,watchdog_retained=1,limit_us=15000000,limit_ok=1):errors.append('outer power lifetime missing')
 order=['marble-npa-client: request index=1 ',markers[2],markers[3],'marble-icb-bw: requested ',
  'marble-adsp-dependency: simultaneous ','marble-icb-bw: completed ','marble-npa-held: returned ',
  'marble-npa-client: destroy index=1 ','marble-adsp-dependency: complete ']
 positions=[text.find(s) for s in order]
 if min(positions)<0 or positions!=sorted(positions):errors.append('power/clock/bandwidth lifetime ordering')
 clean=not any(s in text for s in ('marble-icb-run: complete ','marble-npa-client: create ')) and 'marble-adsp-dependency: refused clean=1 attempted=0 dirty=0' in text
 return dict(passed=not errors,errors=errors,reset_safe=not errors,cleanup_verified=False,clean_refusal=clean,
  simultaneous_dependencies_verified=not errors,original_hardware_restored=False,dsp_started=False,audio_acceptance=False,
  scope='RUN plus physical XO ON, two configured LPI256 requests and 1MB/s bandwidth held together for 10ms; bandwidth/LPI requests released, XO/RUN retained until mandatory cold reset')
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('log',type=Path);a=p.parse_args();r=inspect(a.log.read_text(errors='replace'));print(json.dumps(r,indent=2));raise SystemExit(not r['passed'])
