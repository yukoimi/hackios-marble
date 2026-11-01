"""Read-only accounting of PAS phase time; never a PAS acceptance override."""
import re
def inspect(text):
 errors=[];values={}
 try:
  def rows(prefix):return [dict(re.findall(r'(\w+)=([^\s]+)',line)) for line in text.splitlines() if line.startswith(prefix)]
  summaries=rows('aud-pas-timing: ');calls=rows('aud-pas-time: ');stops=rows('marble-smccc: PAS_stop ')
  if len(summaries)!=1 or len(stops)!=1:raise ValueError('one PAS timing summary and raw stop required')
  values={k:int(v) for k,v in summaries[0].items()}
  version=values.get('version');phases={'entry_us','raise_us','post_us','restore_us'} if version==2 else set()
  if version not in (1,2):raise ValueError('unsupported PAS timing version')
  expected=dict(version=version,limit_us=1000000,limit_calls=128,IRQ_masks_unchanged=1,USB_inside_calls=0,audio_acceptance=0)
  if set(values)!=set(expected)|phases|{'calls','total_us','smc_us','thermal_us','other_us','max_smc_us'} or any(values.get(k)!=v for k,v in expected.items()):raise ValueError('unchanged PAS timing policy required')
  count=values['calls'];total=values['total_us']
  if not 1<=count==len(calls)<=128 or int(stops[0]['calls'])!=count or int(stops[0]['elapsed_us'])!=total:raise ValueError('PAS timing call/time identity')
  smc=thermal=other=maximum=0
  for i,row in enumerate(calls):
   if set(row)!={'index','smc_us','thermal_us','other_us','raw_a0'} or int(row['index'])!=i:raise ValueError('contiguous exact timing records required')
   s,t,o=(int(row[k]) for k in ('smc_us','thermal_us','other_us'))
   if any(n<0 or n>total for n in (s,t,o)):raise ValueError('invalid phase duration')
   a0=int(row['raw_a0'],16)
   if i<count-1 and a0!=1:raise ValueError('non-interrupted intermediate SMC')
   if i==count-1 and a0!=int(stops[0]['raw_a0'],16):raise ValueError('final raw SMC identity')
   smc+=s;thermal+=t;other+=o;maximum=max(maximum,s)
  if (smc,thermal,other,maximum)!=(values['smc_us'],values['thermal_us'],values['other_us'],values['max_smc_us']):raise ValueError('phase totals do not add up')
  outside=sum(values[k] for k in phases)
  if any(values[k]<0 or values[k]>total for k in phases):raise ValueError('invalid entry/TPL phase duration')
  if not 0<=smc+thermal+other+outside<=total or total-(smc+thermal+other+outside)>20000:raise ValueError('unaccounted PAS time exceeds diagnostic tolerance')
 except (KeyError,ValueError,TypeError) as exc:errors.append(str(exc))
 return dict(passed=not errors,errors=errors,record=values,
             PAS_acceptance=False,audio_acceptance=False,
             scope='phase timing consistency only; 1s PAS success/restore/reset remain separate requirements')
