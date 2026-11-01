"""Passive APPS SMMU stream metadata; no DMA programming or ownership claim."""
import re
def inspect(text):
 def rows(prefix):return [dict(re.findall(r'(\w+)=([^\s]+)',l)) for l in text.splitlines() if l.startswith(prefix)]
 def one(prefix):
  r=rows(prefix)
  if len(r)!=1:raise ValueError('missing/duplicate '+prefix)
  return r[0]
 def exact(r,**kw):return all(r.get(k)==str(v) for k,v in kw.items())
 errors=[];matches=[];free=[];mode='unknown';context=None;fault=None
 try:
  if rows('marble-audio-dma: refused '):raise ValueError('snapshot refused')
  if not exact(one('marble-audio-dma: begin '),base='15000000',bytes=4096,SID='1801',MMIO_writes=0):raise ValueError('inventory boundary')
  g=one('marble-audio-dma: geometry ');end=one('marble-audio-dma: complete ')
  v={k:int(g[k],16) for k in ('scr0','id0','id1','gfsr')}
  if not all(0<=x<=0xffffffff for x in v.values()) or v['scr0']&8 or v['id0'] in (0,0xffffffff) or v['id1'] in (0,0xffffffff) or not v['id0']&(1<<27) or not v['id0']&(1<<30) or not 0<(v['id0']&255)<=120 or not 0<(v['id1']&255)<=78:raise ValueError('unsupported SMMU geometry')
  r=rows('marble-audio-dma: route ')
  count=v['id0']&255
  if len(r)!=count:raise ValueError('complete route snapshot required')
  for i,row in enumerate(r):
   if row.get('index')!=str(i):raise ValueError('route index order')
   smr=int(row['smr'],16);s2cr=int(row['s2cr'],16)
   if not 0<=smr<=0xffffffff or not 0<=s2cr<=0xffffffff:raise ValueError('register bounds')
   if not smr>>31:free.append(i)
   elif not (((smr&0x7fff)^0x1801)&~((smr>>16)&0x7fff)):matches.append((i,(s2cr>>16)&3,s2cr&255))
  first,typ,cb=matches[0] if matches else (0xffffffff,0,0)
  if not exact(end,PASS=1,stable=1,routes=count,matches=len(matches),first=f'{first:x}',free=f'{free[0] if free else 0xffffffff:x}',type=typ,context=cb,MMIO_reads=8+4*count,MMIO_writes=0,context_reads=0,DMA_requests=0):raise ValueError('snapshot result mismatch or mutations')
  mode='ambiguous' if len(matches)>1 else ('translate','bypass','fault','reserved')[typ] if matches else 'absent';context=cb if len(matches)==1 and typ==0 else None;fault=v['gfsr']
  order=['marble-pil-boot: auth ','marble-audio-dma: begin ','marble-audio-dma: geometry ','marble-audio-dma: complete ','tr: q s=0 ','marble-smccc: PAS_stop ']
  pos=[text.find(p) for p in order]
  if min(pos)<0 or pos!=sorted(pos):raise ValueError('DSP/RO inventory/graph/PAS ordering')
 except (KeyError,ValueError,TypeError) as e:errors.append(str(e))
 return dict(passed=not errors,errors=errors,read_only_inventory_verified=not errors,audio_stream='0x1801',audio_matches=len(matches),route_mode=mode,audio_context=context,global_fault=fault,MMIO_writes=0,DMA_requests=0,DMA_ready=False,audio_acceptance=False,scope='two stable GR0 route snapshots only; no context ownership, mapping, fault clearing or PCM access')
