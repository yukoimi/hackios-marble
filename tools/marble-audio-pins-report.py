"""Bounded physical TDM pin mux, electrical values, amplifier-reset boundary."""
import re
def inspect(text):
 errors=[]
 try:
  def rows(prefix):return [dict(re.findall(r'(\w+)=([^\s]+)',l)) for l in text.splitlines() if l.startswith(prefix)]
  def one(prefix):
   r=rows(prefix)
   if len(r)!=1:raise ValueError('missing/duplicate '+prefix)
   return r[0]
  def exact(r,**kw):return all(r.get(k)==str(v) for k,v in kw.items())
  if rows('tp: restore_refused '):raise ValueError('pin restore refused')
  if not exact(one('tp: begin '),cfg='2c4,2c4,c4,c5',amps_reset=1) or not exact(one('tp: acquired '),PASS=1,active=1,writes=6) or not exact(one('tp: end '),PASS=1,active=0,restored=1,writes=12,PAS_stopped=0,amp_changes=0):raise ValueError('complete pin lease required')
  pins=rows('tp: pin ');gpios=(1,120,52,121,123,122,124);active=(1,1,1,0x2c4,0x2c4,0xc4,0xc5)
  if len(pins)!=21:raise ValueError('three complete board snapshots required')
  for phase in (0,1,2):
   for i,gpio in enumerate(gpios):
    r=[x for x in pins if exact(x,phase=phase,index=i,gpio=gpio)]
    if len(r)!=1:raise ValueError('pin identity/order')
    cfg=int(r[0]['cfg'],16);io=int(r[0]['io'],16);expected=active[i] if phase==1 else 1;out=2 if phase==1 and i in (3,4) else 0
    if cfg!=expected or not 0<=io<=0xffffffff or (io&2)!=out or (i<3 and io&3):raise ValueError('mux/drive/bias/output/amp boundary')
  order=['tr: r s=2 ','tp: begin ','tp: pin phase=0 index=0 ','tp: pin phase=1 index=6 ','tp: acquired ','pd: q ','pd: r ','tr: r s=4 ','tp: pin phase=2 index=0 ','tp: end ','pm: q s=1 ','marble-smccc: PAS_stop ']
  p=[text.find(x) for x in order]
  if min(p)<0 or p!=sorted(p) or text.find('tr: d s=4 ')>=text.find('tp: pin phase=2 index=0 '):raise ValueError('START/pins/PCM/CLOSE/restore/unmap/PAS order')
 except (KeyError,ValueError,TypeError) as e:errors.append(str(e))
 return dict(passed=not errors,errors=errors,TDM_pins_routed_and_restored=not errors,GPIO_writes=12,amplifier_commands=0,amplifiers_reset=True,audio_acceptance=False,scope='cape GPIO121/123/122/124 function1, board drive/bias/direction, silent PCM consumption and exact configuration/output-latch restore; no measured waveform or speaker playback')
