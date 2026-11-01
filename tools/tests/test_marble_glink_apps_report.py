"""adsp_apps metadata must not be mistaken for GPR/SPF/audio acceptance."""
import importlib.util,unittest
from pathlib import Path
BASE=Path(__file__).parent
s=importlib.util.spec_from_file_location('apps_control_fixture',BASE/'test_marble_glink_control_report.py');f=importlib.util.module_from_spec(s);s.loader.exec_module(f)
EVENTS='\n'.join([
 'marble-glink-control: tx command=4 param1=3 param2=0 local_head=24 bytes=8',
 'marble-glink-control: tx command=2 param1=1 param2=a local_head=48 bytes=24',
 'marble-glink-control: remote_open channel=3 name=adsp_apps deferred=0 acknowledged=1 local_channels=1',
 'marble-glink-control: rx command=2 param1=3 param2=a bytes=24 local_tail=40',
 'marble-glink-apps: open_ack local_channel=1 remote_channel=3 local_intents=0 GPR_packets=0',
 'marble-glink-control: rx command=4 param1=1 param2=0 bytes=8 local_tail=48',
 'marble-glink-apps: remote_intent channel=3 index=0 id=1 bytes=512 local_intents=0 payload_reads=0',
 'marble-glink-apps: remote_intent channel=3 index=1 id=2 bytes=512 local_intents=0 payload_reads=0',
 'marble-glink-control: rx command=5 param1=3 param2=2 bytes=24 local_tail=72'])
END='marble-glink-apps: complete PASS=1 remote_channel=3 local_channel=1 requested=1 open_ACK=1 remote_intents=2 intent_capacity=512 local_intents=0 GPR_packets=0 PAS_stopped=1 retired_after_PAS=1 close_handshake=0 audio_acceptance=0'
def fixture():
 text=f.fixture().replace('marble-pil-boot: observed ',EVENTS+'\nmarble-pil-boot: observed ',1)
 return text.replace('marble-glink-control: complete ',END+'\nmarble-glink-control: complete ',1).replace('RX_messages=2 TX_messages=2 remote_opens=0','RX_messages=5 TX_messages=4 remote_opens=1')
class Apps(unittest.TestCase):
 def inspect(self,text):return f.held.scm.m.inspect(text,1,scm_peak=True,wire=True,raw_shutdown=True,held=True,control=True,apps=True)
 def test_bidirectional_open_metadata_only(self):
  r=self.inspect(fixture());self.assertTrue(r['passed'],r);self.assertTrue(r['adsp_apps_channel_verified']);self.assertFalse(r['audio_acceptance'])
 def test_requires_explicit_app_contract(self):
  self.assertFalse(f.m.inspect(fixture())['passed']);self.assertFalse(self.inspect(f.fixture())['passed'])
  self.assertFalse(f.held.scm.m.inspect(fixture(),1,scm_peak=True,wire=True,raw_shutdown=True,held=True,apps=True)['passed'])
 def test_wrong_identity_or_intent_bounds(self):
  for a,b in [('command=4 param1=3','command=4 param1=2'),('command=2 param1=1','command=2 param1=2'),
   ('name=adsp_apps','name=apr_audio_svc'),('open_ACK=1','open_ACK=0'),('id=2 bytes=512','id=1 bytes=512'),
   ('bytes=512 local_intents','bytes=65537 local_intents'),('param2=2 bytes=24','param2=64 bytes=24'),
   ('local_head=48','local_head=40'),('local_tail=72','local_tail=64'),('remote_channel=3','remote_channel=4')]:
   self.assertFalse(self.inspect(fixture().replace(a,b))['passed'],a)
 def test_data_intents_or_close_claim_not_allowed(self):
  for a,b in [('GPR_packets=0','GPR_packets=1'),('local_intents=0','local_intents=1'),('close_handshake=0','close_handshake=1'),
   ('tx command=2','tx command=9'),('rx command=5','rx command=9'),('retired_after_PAS=1','retired_after_PAS=0')]:
   self.assertFalse(self.inspect(fixture().replace(a,b))['passed'],a)
 def test_partial_or_duplicate_metadata(self):
  text=fixture()
  for line in (*EVENTS.splitlines(),END):
   self.assertFalse(self.inspect(text.replace(line,''))['passed'],line);self.assertFalse(self.inspect(text+'\n'+line)['passed'],line)
if __name__=='__main__':unittest.main()
