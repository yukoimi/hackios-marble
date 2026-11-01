"""Audit the isolated owned-table/ATS/release cycle, not DSP PCM playback."""
import re
def inspect(text,held=False,pcm_data=False):
 errors=[];v={}
 try:
  lines=[l for l in text.splitlines() if l.startswith('marble-audio-map: result ')]
  if len(lines)!=1:raise ValueError('missing/duplicate mapping result')
  row=dict(re.findall(r'(\w+)=([^\s]+)',lines[0]))
  hexkeys={'allocation','physical','par'};v={k:int(x,16 if k in hexkeys else 10) for k,x in row.items()}
  for k,x in dict(PASS=1,stage=9 if held else 7,status=0,context=5,allocated=1,programmed=1,translated=1,disabled=1,restored=1,freed=1,retained=0,stream_writes=8 if held else 4,translation_mode=2,PCM_requests=int(pcm_data)).items():
   if v.get(k)!=x:raise ValueError('mapping boundary/result '+k)
  if not 0<v['allocation']<(1<<36)-16384 or v['allocation']&4095 or v['physical']!=v['allocation']+12288 or v['par']&1 or (v['par']>>12)&0xfffffffff!=v['physical'] or not 20<=v['writes']<=64:raise ValueError('physical translation or bounded writes')
  tbu=[l for l in text.splitlines() if l.startswith('marble-audio-tbu: complete ')]
  ecats=[l for l in text.splitlines() if l.startswith('marble-audio-tbu: ecats ')]
  if len(tbu)!=1 or len(ecats)!=1:raise ValueError('TBU translation/restore evidence missing')
  t=dict(re.findall(r'(\w+)=([^\s]+)',tbu[0]));e=dict(re.findall(r'(\w+)=([^\s]+)',ecats[0]))
  if any(t.get(k)!=str(x) for k,x in dict(PASS=1,idle=1,route_restored=1,micro_idle_restored=1,stream_writes=4).items()) or any(e.get(k)!=str(x) for k,x in dict(status=0,phase=5,completed=1,restored=1,retain=0).items()) or int(e['physical'],16)!=v['physical'] or int(e['par'],16)!=v['par']:raise ValueError('TBU result mismatch')
  order=('marble-audio-context: complete PASS=1','tr: q s=0 ','pm: end ','marble-audio-map: result ','marble-smccc: PAS_stop ') if held else ('marble-audio-context: complete PASS=1','marble-audio-map: result ','tr: q s=0 ','marble-smccc: PAS_stop ')
  p=[text.find(x) for x in order]
  if min(p)<0 or p!=sorted(p):raise ValueError('mapping/graph/shutdown ordering')
 except (KeyError,ValueError,TypeError) as e:errors.append(str(e))
 return dict(passed=not errors,errors=errors,result=v,DMA_ready=False,audio_acceptance=False,scope='one allocated 4K page, actual LPASS TBU ECATS in unreferenced bank, disable/sync/exact configuration restore/free; exact temporary SID route and LPASS micro-idle/ECATS restoration'+('; silent PCM request audited separately' if pcm_data else '; no DSP PCM request'))
