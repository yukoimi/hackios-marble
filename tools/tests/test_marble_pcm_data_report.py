import importlib.util,re,unittest
from pathlib import Path
BASE=Path(__file__).parent
def load(name,path):
 s=importlib.util.spec_from_file_location(name,path);m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m
old=load('data_memory_fixture',BASE/'test_marble_pcm_memory_report.py');m=load('pcm_data_report',BASE.parent/'marble-pcm-data-report.py')
def fixture():
 s=old.fixture().replace('buffers=0','buffers=1').replace('PCM_requests=0','PCM_requests=1').replace('remote_intents=7','remote_intents=8').replace('GPR_packets=7','GPR_packets=8').replace('command=5 param1=3 param2=7 bytes=64','command=5 param1=3 param2=8 bytes=72')
 extra='marble-glink-apps: remote_intent channel=3 index=7 id=8 bytes=128 local_intents=0 payload_reads=0\n'
 at=s.index('prm: pin after=0 ');s=s[:at]+extra+s[at:]
 packet='\n'.join(['marble-glink-control: tx command=9 param1=1 param2=8 local_head=0 bytes=88','pd: q t=4d504401 n=68 id=8 cap=128 handle=80001234 bytes=4096 frames=512 rate=96000 bits=32 channels=2 zero=1','pd: d c=14 id=8','marble-glink-control: rx command=14 param1=3 param2=8 bytes=8 local_tail=0','pd: f c=9 n=56 l=0 u=56 f=72','pd: r t=4d504401 error=0 md=0 kind=1 handle=80001234 n=56','marble-glink-control: tx command=14 param1=1 param2=1 local_head=0 bytes=8','marble-glink-control: rx command=9 param1=3 param2=1 bytes=72 local_tail=0','pd: end PASS=1 sent=1 tx=1 reply=1 kind=1 error=0 md=0 zero=1 faults_ok=1 bad=0 PCM_requests=1 PAS_stopped=0'])+'\n'
 at=s.index('marble-glink-control: tx command=9 param1=1 param2=4 ');s=s[:at]+packet+s[at:]
 head=tail=ntx=nrx=0;out=[]
 for l in s.splitlines():
  if l.startswith('marble-glink-control: tx '):
   head+=int(re.search(r'bytes=(\d+)',l)[1]) if 'bytes=' in l else 8;ntx+=1;l=re.sub(r'local_head=\d+',f'local_head={head}',l)
  if l.startswith('marble-glink-control: rx '):
   tail+=int(re.search(r'bytes=(\d+)',l)[1]);nrx+=1;l=re.sub(r'local_tail=\d+',f'local_tail={tail}',l)
  out.append(l)
 s='\n'.join(out);s=re.sub(r'RX_messages=\d+',f'RX_messages={nrx}',s);s=re.sub(r'TX_messages=\d+',f'TX_messages={ntx}',s);return s
class Data(unittest.TestCase):
 def test_silent_buffer_and_all_lifetimes(self):
  s=fixture();r=m.inspect(s);self.assertTrue(r['passed'],r);self.assertFalse(r['audio_acceptance'])
  run=load('data_run',BASE.parent/'marble-tdm-run-report.py');r=run.inspect(s,pcm_graph=True,pcm_memory=True,pcm_data=True);self.assertTrue(r['passed'],r)
  owned=load('data_owned',BASE.parent/'marble-adsp-owned-report.py');kw=dict(scm_peak=True,wire=True,raw_shutdown=True,held=True,control=True,apps=True,gpr=True,tdm_run=True,tdm_pcm_graph=True,audio_dma=True,pcm_memory=True)
  r=owned.inspect(s,1,pcm_data=True,**kw);self.assertTrue(r['passed'],r);self.assertFalse(owned.inspect(s,1,**kw)['passed'])
 def test_bad_completion_or_wrong_dma_boundary(self):
  for a,b in [('kind=1','kind=2'),('faults_ok=1','faults_ok=0'),('frames=512','frames=1024'),('zero=1','zero=0'),('n=56','n=32'),('handle=80001234','handle=0'),('id=8 cap=128','id=1 cap=128'),('t=4d504401','t=4d504402')]:self.assertFalse(m.inspect(fixture().replace(a,b))['passed'],a)
  for l in fixture().splitlines():
   if l.startswith('pd:'):
    self.assertFalse(m.inspect(fixture().replace(l,''))['passed'],l);self.assertFalse(m.inspect(fixture()+'\n'+l)['passed'],l)
 def test_controller_recovery(self):
  base=load('data_controller',BASE/'test_marble_amdb_report.py').Controller()
  code,r=base.simulate(pcm_data=True);self.assertEqual(code,0,r);self.assertTrue(r['adsp_pcm_data_cycle']['typed_buffer_consumed'])
  for kw in (dict(bad=True),dict(changed=False)):
   code,r=base.simulate(pcm_data=True,**kw);self.assertEqual(code,1,r)
if __name__=='__main__':unittest.main()
