import importlib.util,unittest
from pathlib import Path
BASE=Path(__file__).parent
def load(name,path):
 s=importlib.util.spec_from_file_location(name,path);m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m
apps=load('gpr_apps_fixture',BASE/'test_marble_glink_apps_report.py')
gpr=load('gpr_report',BASE.parent/'marble-gpr-state-report.py')
def fixture():
 text=apps.fixture()
 text='\n'.join(l for l in text.splitlines() if not l.startswith('marble-irq-observer:'))
 text=text.replace('RX_messages=5 TX_messages=4','RX_messages=7 TX_messages=7')
 text=text.replace('intent_capacity=512 local_intents=0 GPR_packets=0','intent_capacity=512 local_intents=1 GPR_packets=1')
 text=text.replace('local_channels=0 GPR_packets=0 PAS_stopped','local_channels=0 GPR_packets=1 PAS_stopped')
 advert='marble-glink-control: tx command=5 param1=1 param2=1 local_head=64 bytes=16\nmarble-gpr: local_intent channel=1 id=1 bytes=512 static=1'
 events='\n'.join([
  'marble-glink-control: tx command=9 param1=1 param2=1 local_head=104 bytes=40',
  'marble-gpr: query token=4d535001 opcode=1001021 bytes=24 remote_id=1 remote_bytes=512 local_intents=1 audio_commands=0',
  'marble-gpr: tx_done command=14 remote_channel=3 remote_id=1',
  'marble-glink-control: rx command=14 param1=3 param2=1 bytes=8 local_tail=80',
  'marble-gpr: fragment command=9 channel=3 intent=1 chunk=28 left=0 accumulated=28 frame_bytes=48',
  'marble-gpr: reply token=4d535001 kind=1 state=1 error=0 bytes=28 remote_channel=3 local_intent=1',
  'marble-glink-control: tx command=14 param1=1 param2=1 local_head=112 bytes=8',
  'marble-glink-control: rx command=9 param1=3 param2=1 bytes=48 local_tail=128'])
 text=text.replace('marble-glink-control: rx command=4 param1=1 param2=0 bytes=8 local_tail=48',
  'marble-glink-control: rx command=4 param1=1 param2=0 bytes=8 local_tail=48\n'+advert)
 text=text.replace('marble-pil-boot: observed ',events+'\nmarble-pil-boot: observed ',1)
 end='marble-gpr: complete sent=1 replies=1 SPF_state=2 tx_done=1 fragment_active=0 fragment_bytes=0 PAS_stopped=1 audio_acceptance=0'
 return text.replace('marble-glink-apps: complete ',
  'marble-irq-observer: omitted profile=GPR-only IRQ_writes=0 IAR_reads=0\n'+end+'\nmarble-glink-apps: complete ',1)
class Gpr(unittest.TestCase):
 def test_ready_and_independent_recovery(self):
  text=fixture();r=gpr.inspect(text);self.assertTrue(r['passed'],r);self.assertTrue(r['spf_ready']);self.assertFalse(r['audio_acceptance'])
  r=apps.f.held.scm.m.inspect(text,1,scm_peak=True,wire=True,raw_shutdown=True,held=True,control=True,apps=True,gpr=True)
  self.assertTrue(r['passed'],r);self.assertTrue(r['reset_safe']);self.assertFalse(r['audio_acceptance'])
 def test_explicit_contract(self):
  self.assertFalse(apps.f.m.inspect(fixture(),apps=True)['passed'])
  self.assertFalse(gpr.inspect(apps.fixture())['passed'])
 def test_not_ready_is_not_ready(self):
  r=gpr.inspect(fixture().replace('kind=1 state=1','kind=1 state=0').replace('SPF_state=2','SPF_state=1'))
  self.assertTrue(r['passed'],r);self.assertFalse(r['spf_ready']);self.assertEqual(r['spf_state'],0)
 def test_basic_alone_is_not_ready(self):
  text=fixture().replace('kind=1 state=1 error=0 bytes=28','kind=2 state=0 error=0 bytes=32').replace('SPF_state=2','SPF_state=0').replace('chunk=28 left=0 accumulated=28','chunk=32 left=0 accumulated=32')
  r=gpr.inspect(text);self.assertTrue(r['passed'],r);self.assertFalse(r['spf_ready'])
 def test_ownership_lengths_and_completion(self):
  for a,b in [('remote_id=1','remote_id=9'),('token=4d535001','token=0'),('opcode=1001021','opcode=1001013'),
   ('bytes=512 static=1','bytes=513 static=1'),('remote_channel=3','remote_channel=2'),
   ('command=9 channel=3','command=12 channel=3'),('accumulated=28','accumulated=29'),('left=0','left=1'),
   ('PAS_stopped=1','PAS_stopped=0'),('tx_done=1','tx_done=0'),('fragment_active=0','fragment_active=1'),
   ('bytes=40','bytes=32'),('state=1 error=0','state=1 error=1'),('audio_commands=0','audio_commands=1')]:
   self.assertFalse(gpr.inspect(fixture().replace(a,b))['passed'],a)
 def test_missing_duplicate_evidence(self):
  text=fixture()
  for line in text.splitlines():
   if line.startswith('marble-gpr:'):
    self.assertFalse(gpr.inspect(text.replace(line,''))['passed'],line)
    self.assertFalse(gpr.inspect(text+'\n'+line)['passed'],line)
if __name__=='__main__':unittest.main()
