import importlib.util,re,unittest
from pathlib import Path
BASE=Path(__file__).parent
def load(name,path):
 s=importlib.util.spec_from_file_location(name,path);m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m
old=load('tdm_run_clock',BASE/'test_marble_audio_clock_report.py')
m=load('tdm_run_report',BASE.parent/'marble-tdm-run-report.py')

def fixture(pcm=False):
 s=old.fixture();start=s.index('marble-glink-control: tx command=9 ');end=s.index('marble-pil-boot: observed ',start)
 before=s[:start].replace('prm: intent channel=1 id=1 bytes=512 static=1','tdm: intent bytes=512')
 if pcm:before=before.replace('index=0 id=1 bytes=512','index=0 id=1 bytes=1024')
 before=before.replace('command=5 param1=3 param2=2 bytes=24','command=5 param1=3 param2=5 bytes=48')
 extra='\n'.join(f'marble-glink-apps: remote_intent channel=3 index={i} id={i+1:x} bytes=512 local_intents=0 payload_reads=0' for i in range(2,5))+'\n'
 before+=extra+'\n'.join(l for l in s.splitlines() if l.startswith(('prm: pin after=0 ','prm: guard after=0 ')))+'\n'
 packets=[]
 for i in range(5):
  token=f'{0x4d545201+i:x}';iid=i+1;n=64 if i else 528 if pcm else 432;frame=80 if i else 544 if pcm else 448
  packets.extend([f'marble-glink-control: tx command=9 param1=1 param2={iid:x} local_head=0 bytes={frame}',f'tdm: query stage={i} token={token} bytes={n} id={iid:x} cap={1024 if pcm and not i else 512}',f'tdm: done stage={i} command=14 id={iid:x}',f'marble-glink-control: rx command=14 param1=3 param2={iid:x} bytes=8 local_tail=0','tdm: fragment command=9 chunk=32 left=0 used=32 frame=48',f'tdm: reply stage={i} token={token} error=0 client=0 bytes=32','marble-glink-control: tx command=14 param1=1 param2=1 local_head=0 bytes=8','marble-glink-control: rx command=9 param1=3 param2=1 bytes=48 local_tail=0'])
 after=s[end:];after=re.sub(r'prm: complete [^\n]*','tdm: complete sent=5 replies=5 done=5 mask=1f tx=1f reply=1f rejected=0 pins=1 fragment=0 bytes=0 errors=0,0,0,0,0',after)
 if pcm:after=after.replace('tdm: complete ','tdm: topology modules=2 links=1 rate=96000 bits=32 channels=2 buffers=0\ntdm: complete ',1)
 s=before+'\n'.join(packets)+'\n'+after
 s=s.replace('remote_intents=2','remote_intents=5').replace('GPR_packets=2','GPR_packets=5')
 head=tail=ntx=nrx=0;lines=[]
 for l in s.splitlines():
  if l.startswith('marble-glink-control: tx '):
   head+=int(re.search(r'bytes=(\d+)',l)[1]) if 'bytes=' in l else 8;ntx+=1;l=re.sub(r'local_head=\d+',f'local_head={head}',l)
  if l.startswith('marble-glink-control: rx '):
   tail+=int(re.search(r'bytes=(\d+)',l)[1]);nrx+=1;l=re.sub(r'local_tail=\d+',f'local_tail={tail}',l)
  lines.append(l)
 s='\n'.join(lines).replace('RX_messages=9',f'RX_messages={nrx}').replace('TX_messages=9',f'TX_messages={ntx}')
 compact=[]
 labels={'complete':'end','intent':'i','query':'q','fragment':'f','reply':'r','done':'d'}
 fields={'stage':'s','token':'t','command':'c','chunk':'n','left':'l','used':'u','frame':'f','client':'c','bytes':'n','sent':'s','rejected':'bad','pins':'p','mask':'m','errors':'e','replies':'rx','done':'d','reply':'r','fragment':'f'}
 for l in s.splitlines():
  if l.startswith('tdm: '):
   tag=l.split()[1];l='tr: '+labels.get(tag,tag)+l[len('tdm: '+tag):]
   for a,b in fields.items():l=re.sub(r'\b'+a+'=',b+'=',l)
  compact.append(l)
 return '\n'.join(compact)

class Run(unittest.TestCase):
 def test_lifecycle_and_independent_stop(self):
  s=fixture();r=m.inspect(s);self.assertTrue(r['passed'],r);self.assertFalse(r['audio_acceptance'])
  owned=old.old.old.old.apps.f.held.scm.m
  r=owned.inspect(s,1,scm_peak=True,wire=True,raw_shutdown=True,held=True,control=True,apps=True,gpr=True,tdm_run=True);self.assertTrue(r['passed'],r)
  self.assertFalse(owned.inspect(s,1,scm_peak=True,wire=True,raw_shutdown=True,held=True,control=True,apps=True,gpr=True)['passed'])
 def test_wrong_missing_duplicate_evidence(self):
  s=fixture()
  for a,b in [('m=1f','m=f'),('e=0,0,0,0,0','e=0,0,2,0,0'),('index=0 cfg=1 io=0','index=0 cfg=1 io=2'),('bytes=448','bytes=416'),('t=4d545205','t=4d545204')]:self.assertFalse(m.inspect(s.replace(a,b))['passed'],a)
  for l in s.splitlines():
   if l.startswith(('tr:','prm:')):
    self.assertFalse(m.inspect(s.replace(l,''))['passed'],l);self.assertFalse(m.inspect(s+'\n'+l)['passed'],l)
 def test_controller(self):
  base=load('tdm_run_controller',BASE/'test_marble_amdb_report.py').Controller()
  code,r=base.simulate(tdm_run=True);self.assertEqual(code,0,r);self.assertTrue(r['adsp_tdm_run_cycle']['silent_TDM_lifecycle_verified'])
  for kw in (dict(bad=True),dict(changed=False)):
   code,r=base.simulate(tdm_run=True,**kw);self.assertEqual(code,1,r)
class ConnectedPCM(unittest.TestCase):
 def test_connected_graph_requires_explicit_topology(self):
  s=fixture(pcm=True);r=m.inspect(s,pcm_graph=True);self.assertTrue(r['passed'],r);self.assertTrue(r['connected_PCM_graph_lifecycle_verified']);self.assertFalse(m.inspect(s)['passed'])
  owned=old.old.old.old.apps.f.held.scm.m
  r=owned.inspect(s,1,scm_peak=True,wire=True,raw_shutdown=True,held=True,control=True,apps=True,gpr=True,tdm_run=True,tdm_pcm_graph=True);self.assertTrue(r['passed'],r)
  self.assertFalse(owned.inspect(s,1,scm_peak=True,wire=True,raw_shutdown=True,held=True,control=True,apps=True,gpr=True,tdm_run=True)['passed'])
  for a,b in [('modules=2','modules=1'),('buffers=0','buffers=1'),('rate=96000','rate=48000'),('bytes=544','bytes=448'),('n=528','n=432')]:self.assertFalse(m.inspect(s.replace(a,b),pcm_graph=True)['passed'],a)
 def test_connected_controller_success_and_recovery(self):
  base=load('pcm_graph_controller',BASE/'test_marble_amdb_report.py').Controller()
  code,r=base.simulate(pcm_graph=True);self.assertEqual(code,0,r);self.assertTrue(r['adsp_tdm_run_cycle']['connected_PCM_graph_lifecycle_verified'])
  for kw in (dict(bad=True),dict(changed=False)):
   code,r=base.simulate(pcm_graph=True,**kw);self.assertEqual(code,1,r)
if __name__=='__main__':unittest.main()
