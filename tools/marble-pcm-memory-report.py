"""Typed DSP MAP/UNMAP and ownership lifetime. No PCM playback acceptance."""
import re
def inspect(text,pcm_data=False):
 errors=[];handle=None
 try:
  def rows(prefix):return [dict(re.findall(r'(\w+)=([^\s]+)',l)) for l in text.splitlines() if l.startswith(prefix)]
  def one(prefix):
   r=rows(prefix)
   if len(r)!=1:raise ValueError('missing/duplicate '+prefix)
   return r[0]
  def exact(r,**kw):return all(r.get(k)==str(v) for k,v in kw.items())
  if not exact(one('pm: lease '),PASS=1,active=1,route=1,bytes=4096,address='110000000'):raise ValueError('owned SID/4K lease missing')
  end=one('pm: end ')
  if not exact(end,PASS=1,sent=3,tx=3,reply=3,error='0,0',bad=0,route_restored=1,cleanup=1,PAS_stopped=0,PCM_requests=int(pcm_data)):raise ValueError('typed unmap and early exact cleanup required')
  q=rows('pm: q ');r=rows('pm: r ');d=rows('pm: d ')
  if len(q)!=2 or len(r)!=2 or len(d)!=2:raise ValueError('memory transaction counts')
  handle=int(r[0]['handle'],16)
  if not 0<handle<=0xffffffff or int(end['handle'],16)!=handle or int(r[1]['handle'],16):raise ValueError('typed handle identity')
  intents=rows('marble-glink-apps: remote_intent ');tx=rows('marble-glink-control: tx ');rx=rows('marble-glink-control: rx ')
  used={int(x['id'],16) for x in rows('tr: q ')};ids=[]
  for i in (0,1):
   token=f'{0x4d504d01+i:x}';n=(44,28)[i];iid=int(q[i]['id'],16);cap=int(q[i]['cap']);ids.append(iid)
   if iid in used or iid in ids[:i] or not n<=cap<=65536 or sum(int(x['id'],16)==iid and int(x['bytes'])==cap for x in intents)!=1:raise ValueError('owned distinct intent')
   if not exact(q[i],s=i,t=token,n=n) or not exact(r[i],s=i,t=token,error=0,n=(28,32)[i]) or not 0<=int(r[i]['c'],16)<=255:raise ValueError('typed reply identity')
   done=[x for x in d if x.get('s')==str(i)]
   if len(done)!=1 or done[0]['c'] not in ('6','14') or int(done[0]['id'],16)!=iid:raise ValueError('TX not retired')
   packets=[x for x in tx if x['command']=='9' and int(x['param2'],16)==iid]
   if len(packets)!=1 or not exact(packets[0],param1=1,bytes=(64,48)[i]):raise ValueError('canonical memory DATA size')
   qp=text.find(f'pm: q s={i} ');rp=text.find(f'pm: r s={i} ');dp=text.find(f'pm: d s={i} ')
   if min(qp,rp,dp)<0 or qp>=min(rp,dp):raise ValueError('reply/retirement before request')
  fs=rows('pm: f ');packet=acc=left=0
  for f in fs:
   cmd=int(f['c']);chunk=int(f['n']);remaining=int(f['l']);total=int(f['u'])
   if not 0<chunk<=512 or not 0<=remaining<=512-chunk or int(f['f'])!=((16+chunk+7)&~7) or (not acc and cmd!=9) or (acc and (cmd!=12 or remaining!=left-chunk)) or total!=acc+chunk:raise ValueError('memory fragment continuity')
   acc=total;left=remaining
   if not left:
    if packet>=2 or acc!=(28,32)[packet]:raise ValueError('memory assembly size')
    packet+=1;acc=0
  if acc or packet!=2:raise ValueError('memory assembly incomplete')
  order=['pm: lease ','pm: q s=0 ','pm: r s=0 ','tr: q s=0 ','tr: r s=4 ','pm: q s=1 ','pm: r s=1 ','pm: end ','marble-audio-map: result ','marble-smccc: PAS_stop ']
  pos=[text.find(x) for x in order]
  if min(pos)<0 or pos!=sorted(pos) or max(text.find('pm: r s=0 '),text.find('pm: d s=0 '))>=text.find('tr: q s=0 ') or max(text.find('tr: r s=4 '),text.find('tr: d s=4 '))>=text.find('pm: q s=1 '):raise ValueError('map/graph/unmap/free/PAS lifetime')
 except (KeyError,ValueError,TypeError) as e:errors.append(str(e))
 return dict(passed=not errors,errors=errors,handle=handle,shared_region_bytes=4096,PCM_requests=int(pcm_data),audio_acceptance=False,scope='owned SID1801/4K mapping, typed DSP MAP handle, connected graph lifecycle, typed UNMAP retirement then exact route/context restore/free before PAS'+('; silent PCM consumption audited separately' if pcm_data else '; no PCM submission'))
