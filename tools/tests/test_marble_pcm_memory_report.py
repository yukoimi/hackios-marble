import importlib.util,re,unittest
from pathlib import Path
BASE=Path(__file__).parent
def load(name,path):
 s=importlib.util.spec_from_file_location(name,path);m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m
old=load('memory_map_fixture',BASE/'test_marble_audio_map_report.py')
m=load('pcm_memory_report',BASE.parent/'marble-pcm-memory-report.py')
def transaction(i):
 iid=6+i;token=f'{0x4d504d01+i:x}';n=(44,28)[i]
 return '\n'.join([f'marble-glink-control: tx command=9 param1=1 param2={iid:x} local_head=0 bytes={(64,48)[i]}',f'pm: q s={i} t={token} n={n} id={iid:x} cap=128',f'pm: d s={i} c=14 id={iid:x}',f'marble-glink-control: rx command=14 param1=3 param2={iid:x} bytes=8 local_tail=0',f'pm: f c=9 n={(28,32)[i]} l=0 u={(28,32)[i]} f=48',f'pm: r s={i} t={token} error=0 handle={"80001234" if not i else "0"} c=60 n={(28,32)[i]}','marble-glink-control: tx command=14 param1=1 param2=1 local_head=0 bytes=8','marble-glink-control: rx command=9 param1=3 param2=1 bytes=48 local_tail=0'])+'\n'
def fixture():
 s=old.fixture();lines=s.splitlines();inventory=[l for l in lines if l.startswith(('marble-audio-dma:','marble-audio-context:','marble-audio-tbu:'))]
 result=next(l for l in lines if l.startswith('marble-audio-map:')).replace('stage=7','stage=9').replace('stream_writes=4','stream_writes=8')
 s='\n'.join(l for l in lines if not l.startswith(('marble-audio-dma:','marble-audio-context:','marble-audio-tbu:','marble-audio-map:')))
 at=s.index('marble-glink-control: tx command=9 ')
 s=s[:at]+'\n'.join(inventory)+'\npm: lease PASS=1 active=1 route=1 bytes=4096 address=110000000\n'+transaction(0)+s[at:]
 marker='marble-pil-boot: observed '
 ending='pm: end PASS=1 sent=3 tx=3 reply=3 handle=80001234 error=0,0 bad=0 route_restored=1 cleanup=1 PAS_stopped=0 PCM_requests=0\n'+result+'\n'
 s=s.replace(marker,transaction(1)+ending+marker,1)
 marker='prm: pin after=0 '
 extra='\n'.join(f'marble-glink-apps: remote_intent channel=3 index={i} id={i+1:x} bytes=128 local_intents=0 payload_reads=0' for i in (5,6))+'\n'
 at=s.index(marker);s=s[:at]+extra+s[at:]
 s=s.replace('remote_intents=5','remote_intents=7').replace('GPR_packets=5','GPR_packets=7').replace('command=5 param1=3 param2=5 bytes=48','command=5 param1=3 param2=7 bytes=64')
 head=tail=ntx=nrx=0;out=[]
 for l in s.splitlines():
  if l.startswith('marble-glink-control: tx '):
   head+=int(re.search(r'bytes=(\d+)',l)[1]) if 'bytes=' in l else 8;ntx+=1;l=re.sub(r'local_head=\d+',f'local_head={head}',l)
  if l.startswith('marble-glink-control: rx '):
   tail+=int(re.search(r'bytes=(\d+)',l)[1]);nrx+=1;l=re.sub(r'local_tail=\d+',f'local_tail={tail}',l)
  out.append(l)
 s='\n'.join(out);s=re.sub(r'RX_messages=\d+',f'RX_messages={nrx}',s);s=re.sub(r'TX_messages=\d+',f'TX_messages={ntx}',s)
 return s
class Memory(unittest.TestCase):
 def test_full_ownership_lifetime_and_transport(self):
  s=fixture();r=m.inspect(s);self.assertTrue(r['passed'],r)
  self.assertTrue(old.m.inspect(s,held=True)['passed'])
  run=load('memory_run',BASE.parent/'marble-tdm-run-report.py');r=run.inspect(s,pcm_graph=True,pcm_memory=True);self.assertTrue(r['passed'],r)
  owned=load('memory_owned',BASE.parent/'marble-adsp-owned-report.py');kw=dict(scm_peak=True,wire=True,raw_shutdown=True,held=True,control=True,apps=True,gpr=True,tdm_run=True,tdm_pcm_graph=True,audio_dma=True)
  r=owned.inspect(s,1,pcm_memory=True,**kw);self.assertTrue(r['passed'],r)
  self.assertFalse(owned.inspect(s,1,**kw)['passed'])
 def test_failed_handle_retirement_or_cleanup(self):
  for a,b in [('handle=80001234','handle=0'),('reply=3','reply=1'),('cleanup=1','cleanup=0'),('PAS_stopped=0','PAS_stopped=1'),('n=44','n=60'),('id=6 cap=128','id=1 cap=128'),('t=4d504d02','t=4d504d01')]:self.assertFalse(m.inspect(fixture().replace(a,b))['passed'],a)
  for l in fixture().splitlines():
   if l.startswith('pm:'):
    self.assertFalse(m.inspect(fixture().replace(l,''))['passed'],l);self.assertFalse(m.inspect(fixture()+'\n'+l)['passed'],l)
 def test_controller(self):
  base=load('memory_controller',BASE/'test_marble_amdb_report.py').Controller()
  code,r=base.simulate(pcm_memory=True);self.assertEqual(code,0,r);self.assertTrue(r['adsp_pcm_memory_cycle']['passed'])
  for kw in (dict(bad=True),dict(changed=False)):
   code,r=base.simulate(pcm_memory=True,**kw);self.assertEqual(code,1,r)
if __name__=='__main__':unittest.main()
