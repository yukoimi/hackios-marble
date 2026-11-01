"""Independently validate passive candidate context words against all routes."""
import re

def inspect(text):
 errors=[];context=None;values=[]
 try:
  def rows(prefix):return [dict(re.findall(r'(\w+)=([^\s]+)',l)) for l in text.splitlines() if l.startswith(prefix)]
  def one(prefix):
   r=rows(prefix)
   if len(r)!=1:raise ValueError('missing/duplicate '+prefix)
   return r[0]
  if rows('marble-audio-context: refused '):raise ValueError('candidate refused')
  b=one('marble-audio-context: begin ');end=one('marble-audio-context: complete ');g=one('marble-audio-dma: geometry ')
  context=int(b['context']);id1=int(g['id1'],16);count=int(g['id0'],16)&255
  if id1>>28!=6 or not (id1>>16&255)<=context<(id1&255) or b['base']!=f'{0x15080000+4096*context:x}' or b['MMIO_writes']!='0' or int(g['gfsr'],16):raise ValueError('context geometry')
  routes=rows('marble-audio-dma: route ')
  if len(routes)!=count:raise ValueError('route snapshot incomplete')
  for r in routes:
   smr=int(r['smr'],16);s2cr=int(r['s2cr'],16)
   if smr>>31 and ((s2cr&255)==context or not (((smr&0x7fff)^0x1801)&~(smr>>16&0x7fff))):raise ValueError('context or SID already routed')
  words=rows('marble-audio-context: word ')
  if len(words)!=15:raise ValueError('context snapshot incomplete')
  for i,r in enumerate(words):
   v=int(r['value'],16)
   if r['index']!=str(i) or not 0<=v<=0xffffffff or v!=int(r['second'],16):raise ValueError('context changed')
   values.append(v)
  if any(values[i] for i in (2,3,4,5)) or values[0]&~0xe0 or values[9]&0xc00001fe or values[10]&1:raise ValueError('context has live state')
  expected=dict(PASS='1',stable='1',inactive='1',reads='30',MMIO_writes='0',DMA_requests='0',ownership='0')
  if end!=expected:raise ValueError('context result mismatch')
  order=['marble-audio-dma: complete ','marble-audio-context: begin ','marble-audio-context: complete ','tr: q s=0 ']
  pos=[text.find(p) for p in order]
  if min(pos)<0 or pos!=sorted(pos):raise ValueError('context audit ordering')
 except (KeyError,ValueError,TypeError) as e:errors.append(str(e))
 return dict(passed=not errors,errors=errors,context=context,words=values,MMIO_writes=0,DMA_ready=False,audio_acceptance=False,scope='two passive snapshots of one unreferenced inactive context; no ownership granted')
