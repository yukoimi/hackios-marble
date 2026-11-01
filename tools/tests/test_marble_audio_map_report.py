import importlib.util,unittest
from pathlib import Path
BASE=Path(__file__).parent
def load(name,path):
 s=importlib.util.spec_from_file_location(name,path);m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m
ctx=load('map_context_fixture',BASE/'test_marble_audio_context_report.py')
m=load('map_report',BASE.parent/'marble-audio-map-report.py')
def fixture():
 line='marble-audio-map: result PASS=1 stage=7 status=0 context=5 allocation=90000000 physical=90003000 par=90003000000 allocated=1 programmed=1 translated=1 disabled=1 restored=1 freed=1 retained=0 writes=31 translation_mode=2 stream_writes=4 PCM_requests=0'
 tbu='marble-audio-tbu: ecats status=0 phase=5 completed=1 restored=1 retain=0 par=90003000000 physical=90003000 writes=11 polls=3\nmarble-audio-tbu: complete PASS=1 idle=1 route_restored=1 micro_idle_restored=1 stream_writes=4 TBU_writes=11'
 return ctx.fixture().replace('tr: q s=0 ',tbu+'\n'+line+'\ntr: q s=0 ',1)
class Mapping(unittest.TestCase):
 def test_cycle_never_claims_pcm(self):
  r=m.inspect(fixture());self.assertTrue(r['passed'],r);self.assertFalse(r['DMA_ready'])
 def test_failures_and_foreign_memory(self):
  for a,b in [('PASS=1 stage=7','PASS=0 stage=7'),('physical=90003000','physical=90004000'),('par=90003000000','par=90003000001'),('retained=0','retained=1'),('freed=1','freed=0'),('restored=1','restored=0'),('stream_writes=4','stream_writes=5'),('PCM_requests=0','PCM_requests=1'),('writes=31','writes=100')]:self.assertFalse(m.inspect(fixture().replace(a,b))['passed'],a)
  line=[l for l in fixture().splitlines() if l.startswith('marble-audio-map:')][0]
  self.assertFalse(m.inspect(fixture().replace(line,''))['passed']);self.assertFalse(m.inspect(fixture()+'\n'+line)['passed'])
if __name__=='__main__':unittest.main()
