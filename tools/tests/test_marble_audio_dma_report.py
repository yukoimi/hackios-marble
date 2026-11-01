import importlib.util,unittest
from pathlib import Path
BASE=Path(__file__).parent
def load(name,path):
 s=importlib.util.spec_from_file_location(name,path);m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m
old=load('dma_graph_fixture',BASE/'test_marble_tdm_run_report.py');m=load('audio_dma_report',BASE.parent/'marble-audio-dma-report.py')
def fixture():
 s=old.fixture(pcm=True);log=['marble-audio-dma: begin base=15000000 bytes=4096 SID=1801 MMIO_writes=0','marble-audio-dma: geometry scr0=0 id0=48000078 id1=4e gfsr=0']
 log.extend(f'marble-audio-dma: route index={i} smr={"80001801" if i==3 else "0"} s2cr={"10000" if i==3 else "0"}' for i in range(120))
 log.append('marble-audio-dma: complete PASS=1 stable=1 routes=120 matches=1 first=3 free=0 type=1 context=0 MMIO_reads=488 MMIO_writes=0 context_reads=0 DMA_requests=0')
 return s.replace('tr: q s=0 ','\n'.join(log)+'\ntr: q s=0 ',1)
class Dma(unittest.TestCase):
 def test_route_metadata_and_independent_recovery(self):
  s=fixture();r=m.inspect(s);self.assertTrue(r['passed'],r);self.assertEqual(r['route_mode'],'bypass');self.assertFalse(r['DMA_ready'])
  owned=old.old.old.old.old.apps.f.held.scm.m
  r=owned.inspect(s,1,scm_peak=True,wire=True,raw_shutdown=True,held=True,control=True,apps=True,gpr=True,tdm_run=True,tdm_pcm_graph=True,audio_dma=True);self.assertTrue(r['passed'],r)
  self.assertFalse(owned.inspect(s,1,scm_peak=True,wire=True,raw_shutdown=True,held=True,control=True,apps=True,gpr=True,tdm_run=True,tdm_pcm_graph=True)['passed'])
 def test_real_marble_counts_read_no_absent_tail(self):
  s=fixture().replace('id0=48000078 id1=4e','id0=4c017e71 id1=6000004b').replace('routes=120','routes=113').replace('MMIO_reads=488','MMIO_reads=460')
  for i in range(113,120):s=s.replace(f'marble-audio-dma: route index={i} smr=0 s2cr=0\n','')
  r=m.inspect(s);self.assertTrue(r['passed'],r)
 def test_invalid_incomplete_or_mutating_inventory(self):
  s=fixture()
  for a,b in [('stable=1','stable=0'),('MMIO_writes=0','MMIO_writes=1'),('DMA_requests=0','DMA_requests=1'),('id0=48000078','id0=48000079'),('base=15000000','base=3dc0000'),('smr=80001801','smr=80001802')]:self.assertFalse(m.inspect(s.replace(a,b))['passed'],a)
  for l in s.splitlines():
   if l.startswith('marble-audio-dma:'):
    self.assertFalse(m.inspect(s.replace(l,''))['passed'],l);self.assertFalse(m.inspect(s+'\n'+l)['passed'],l)
 def test_controller_requires_read_only_audit_and_reset(self):
  base=load('audio_dma_controller',BASE/'test_marble_amdb_report.py').Controller()
  code,r=base.simulate(audio_dma=True);self.assertEqual(code,0,r);self.assertTrue(r['adsp_audio_dma_inventory']['read_only_inventory_verified'])
  for kw in (dict(bad=True),dict(changed=False)):
   code,r=base.simulate(audio_dma=True,**kw);self.assertEqual(code,1,r)
if __name__=='__main__':unittest.main()
