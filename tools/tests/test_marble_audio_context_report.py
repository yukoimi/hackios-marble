import importlib.util,unittest
from pathlib import Path
BASE=Path(__file__).parent
def load(name,path):
 s=importlib.util.spec_from_file_location(name,path);m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m
dma=load('context_dma_fixture',BASE/'test_marble_audio_dma_report.py')
m=load('context_report',BASE.parent/'marble-audio-context-report.py')
def fixture():
 s=dma.fixture().replace('smr=80001801','smr=80000001').replace('matches=1 first=3','matches=0 first=ffffffff').replace('type=1','type=0').replace('id1=4e','id1=6000004e')
 lines=['marble-audio-context: begin context=5 base=15085000 MMIO_writes=0']
 lines.extend(f'marble-audio-context: word index={i} value=0 second=0' for i in range(15))
 lines.append('marble-audio-context: complete PASS=1 stable=1 inactive=1 reads=30 MMIO_writes=0 DMA_requests=0 ownership=0')
 return s.replace('tr: q s=0 ','\n'.join(lines)+'\ntr: q s=0 ',1)
class Context(unittest.TestCase):
 def test_inactive_is_not_dma_ownership(self):
  r=m.inspect(fixture());self.assertTrue(r['passed'],r);self.assertFalse(r['DMA_ready'])
 def test_disabled_reset_signature(self):
  s=fixture().replace('index=0 value=0 second=0','index=0 value=e0 second=e0').replace('index=9 value=0 second=0','index=9 value=400 second=400')
  self.assertTrue(m.inspect(s)['passed'])
  self.assertFalse(m.inspect(s.replace('value=400 second=400','value=402 second=402'))['passed'])
 def test_refuse_live_or_conflicting_or_incomplete(self):
  s=fixture()
  for a,b in [('index=0 value=0 second=0','index=0 value=1 second=1'),('index=2 value=0 second=0','index=2 value=1 second=1'),('index=12 value=0 second=0','index=12 value=0 second=1'),('smr=80000001 s2cr=10000','smr=80000001 s2cr=5'),('reads=30','reads=29'),('ownership=0','ownership=1'),('base=15085000','base=15086000')]:self.assertFalse(m.inspect(s.replace(a,b))['passed'],a)
  for l in s.splitlines():
   if l.startswith('marble-audio-context:'):
    self.assertFalse(m.inspect(s.replace(l,''))['passed'],l);self.assertFalse(m.inspect(s+'\n'+l)['passed'],l)
if __name__=='__main__':unittest.main()
