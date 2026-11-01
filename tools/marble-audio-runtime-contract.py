"""Validate the explicitly identified600ms digital-only diagnostic budget."""
import re
def contract(text):
 if any(line.startswith('pr:') for line in text.splitlines()):
  import importlib.util
  from pathlib import Path
  spec=importlib.util.spec_from_file_location('runtime_repeat_report',Path(__file__).with_name('marble-pcm-repeat-report.py'))
  repeat=importlib.util.module_from_spec(spec);spec.loader.exec_module(repeat)
  ends=[dict(re.findall(r'(\w+)=([^\s]+)',line)) for line in text.splitlines() if line.startswith('pr: end ')]
  if len(ends)!=1:raise ValueError('runtime requires one repeat completion')
  requests=int(ends[0].get('submitted','0'))
  r=repeat.inspect(text,requests=requests,mfc=any(line.startswith('mfc: path ') for line in text.splitlines()))
  if not r['passed']:raise ValueError('runtime requires every retired PCM request: '+str(r['errors']))

 analog='marble-aw-analog:' in text or 'phase=analog-' in text
 if analog:
  for device in ('34','35'):
   rows=[dict(re.findall(r'(\w+)=([^\s]+)',l)) for l in text.splitlines() if l.startswith('marble-aw-analog: complete ') and f'device={device} ' in l]
   if len(rows)!=1 or any(rows[0].get(k)!=str(v) for k,v in dict(PASS=1,attempted=1,writes=2,ready=1,restored=1,dirty=0,HMUTE_retained=1,attenuation_db=90,speaker_switch_writes=0,audio_acceptance=0).items()) or not 1<=int(rows[0].get('polls','0'))<=10 or int(rows[0].get('status','0'),16)&0xcfbb!=0x311:raise ValueError('runtime requires both restored muted analog tests')
 typed=[l for l in text.splitlines() if l.startswith('aud-typed-budget:')]
 if typed:
  if len(typed)!=1 or 'aud-budget:' in text or 'aud-capture:' in text:raise ValueError('conflicting typed runtime budget')
  r=dict(re.findall(r'(\w+)=([^\s]+)',typed[0]))
  expected=dict(version=1,minimum_us=300000,maximum_us=600000,poll_rounds=60,completion_required=1,final_thermal_required=1,audio_acceptance=0)
  if r!={k:str(v) for k,v in expected.items()}:raise ValueError('unqualified typed runtime budget')
  early=[dict(re.findall(r'(\w+)=([^\s]+)',l)) for l in text.splitlines() if l.startswith('aud-early:')]
  if len(early)!=1:raise ValueError('typed completion not reached within runtime budget')
  e=early[0];round_=int(e.get('round','-1'));samples=int(e.get('thermal_samples','0'))
  expected=dict(typed_complete=1,minimum_us=300000,maximum_us=750000,final_thermal_ok=1,idle_dwell_omitted=1,PAS_required=1,audio_acceptance=0)
  if not 30<=round_<60 or samples!=round_//10+1 or any(e.get(k)!=str(v) for k,v in expected.items()):raise ValueError('typed completion final thermal/round contract')
  for prefix in ('pd: end PASS=1','pm: end PASS=1','tr: end s=5 rx=5 d=5 m=1f tx=1f r=1f bad=0 p=1'):
   if sum(l.startswith(prefix) for l in text.splitlines())!=1:raise ValueError('typed runtime requires retired PCM/UNMAP/graph')
  for device in ('34','35'):
   rows=[dict(re.findall(r'(\w+)=([^\s]+)',l)) for l in text.splitlines() if l.startswith('marble-aw-digital: complete ') and f'device={device} ' in l]
   if len(rows)!=1 or any(rows[0].get(k)!=str(v) for k,v in dict(PASS=1,locked=1,restored=1,dirty=0,AMPPD_retained=0 if analog else 1,HMUTE_retained=1,attenuation_db=90,speaker_switch_writes=0,audio_acceptance=0).items()):raise ValueError('typed runtime requires both restored muted PLL tests')
  return dict(hold_limit_ms=600,thermal_samples=samples,minimum_us=300000,maximum_us=750000)
 lines=[l for l in text.splitlines() if l.startswith('aud-budget:')]
 if not lines:return dict(hold_limit_ms=500,thermal_samples=5,minimum_us=500000,maximum_us=750000)
 if len(lines)!=1:raise ValueError('duplicate digital runtime budget')
 r=dict(re.findall(r'(\w+)=([^\s]+)',lines[0]))
 expected=dict(version=1,maximum_us=600000,poll_rounds=60,thermal_samples=6,IRQ_masks_unchanged=1,frequency_requests=0,voltage_requests=0,audio_acceptance=0)
 if r!={k:str(v) for k,v in expected.items()}:raise ValueError('unqualified digital runtime budget')
 if 'aud-early:' in text or 'aud-capture:' in text:raise ValueError('600ms budget cannot be combined with early/capture diagnostics')
 for device in ('34','35'):
  rows=[dict(re.findall(r'(\w+)=([^\s]+)',l)) for l in text.splitlines() if l.startswith('marble-aw-digital: complete ') and f'device={device} ' in l]
  if len(rows)!=1 or any(rows[0].get(k)!=str(v) for k,v in dict(PASS=1,locked=1,restored=1,dirty=0,AMPPD_retained=0 if analog else 1,HMUTE_retained=1,attenuation_db=90,speaker_switch_writes=0,audio_acceptance=0).items()):raise ValueError('600ms budget requires both successful muted digital tests')
 return dict(hold_limit_ms=600,thermal_samples=6,minimum_us=600000,maximum_us=750000)
