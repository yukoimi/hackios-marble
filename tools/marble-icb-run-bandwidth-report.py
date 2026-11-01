"""Require native RUN transition before the independently verified request/release."""
import argparse,importlib.util,json
from pathlib import Path
def load(name,file):
 s=importlib.util.spec_from_file_location(name,Path(__file__).with_name(file));m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m
native=load('native_icb_run','marble-icb-run-report.py');bandwidth=load('native_icb_bandwidth','marble-icb-bandwidth-report.py')
def inspect(text,scm_peak=False):
 begin='marble-icb-run-bandwidth: begin contract=native-run-request-reset-v1\n'
 marker='marble-icb-run-bandwidth: phase=bandwidth baseline=after_native_post_init original_firmware_restoration=0\n'
 errors=[];start=text.find(begin);parts=text[start:].split(marker) if start>=0 else [text]
 if text.count(begin)!=1 or len(parts)!=2:errors.append('native RUN/owned request phase boundary missing')
 run=native.inspect(parts[0]);bw=bandwidth.inspect(parts[-1] if len(parts)==2 else '',True,peak=0xffffffff*1000 if scm_peak else 1000000)
 errors.extend('run: '+e for e in run['errors']);errors.extend('bandwidth: '+e for e in bw['errors'])
 return dict(passed=not errors,errors=errors,reset_safe=not errors,cleanup_verified=False,
  clean_refusal=run['clean_refusal'] and len(parts)==1,
  native_run_transition_verified=run['passed'],bounded_bandwidth_request_verified=bw['passed'],
  scm_peak_request_verified=scm_peak and bw['passed'],
  original_firmware_restored=False,dsp_started=False,audio_acceptance=False,
  scope=('native post-init/RUN, followed by Linux SCM logical peak request/capped BCM ACKs' if scm_peak else 'native post-init/RUN, followed by 1MB/s request')+' and complete/destroy with original RUN aggregates and cache values restored; firmware state requires cold reset; no throughput/DSP proof')
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('log',type=Path);a=p.parse_args();r=inspect(a.log.read_text(errors='replace'));print(json.dumps(r,indent=2));raise SystemExit(not r['passed'])
