"""Keep the AP-only wire experiment inside the full no-exec power lifetime."""
import importlib.util,unittest
from pathlib import Path
BASE=Path(__file__).parent
def load(name,file):
 s=importlib.util.spec_from_file_location(name,BASE/file);m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m
scm=load('wire_scm_fixture','test_marble_adsp_scm_report.py')
wire=load('wire_mailbox_fixture','test_marble_qmp_wire_report.py')
def fixture():
 text=scm.fixture();pos=text.index('marble-pil-held: shutdown ')
 return text[:pos]+'marble-pil-progress: AP-wire-post-load-before address=1\n'+wire.LOG+'\nmarble-pil-progress: AP-wire-noexec-unwind-return address=1\n'+text[pos:]
class OwnedWire(unittest.TestCase):
 def inspect(self,text):return scm.m.inspect(text,scm_peak=True,wire=True)
 def test_full_lifetime(self):
  r=self.inspect(fixture());self.assertTrue(r['passed'],r);self.assertTrue(r['AP_wire_noexec_unwind_verified']);self.assertFalse(r['audio_acceptance']);self.assertFalse(r['dsp_started'])
 def test_explicit_contract_and_power(self):
  self.assertFalse(scm.m.inspect(fixture(),scm_peak=True)['passed'])
  self.assertFalse(scm.m.inspect(fixture(),wire=True)['passed'])
  self.assertFalse(scm.m.inspect(fixture(),wire=True,qmp=True,scm_peak=True)['passed'])
  self.assertFalse(self.inspect(scm.fixture())['passed'])
 def test_unsafe_or_partial_unwind(self):
  for a,b in [('Clock_after=0','Clock_after=1'),('AP_ack=ffff0000','AP_ack=ffff'),('descriptor_restored=1','descriptor_restored=0'),
    ('AP-wire-noexec-unwind-return address=1','AP-wire-noexec-unwind-return address=0'),('ROM_stage_restored=1','ROM_stage_restored=0'),
    ('peak_hold PASS=1','peak_hold PASS=0'),('PAS_calls=0','PAS_calls=1')]:
   self.assertFalse(self.inspect(fixture().replace(a,b))['passed'],a)
 def test_wrong_order(self):
  text=fixture();start=text.index('marble-pil-progress: AP-wire-post-load-before');end=text.index('marble-pil-held: shutdown ',start)
  block=text[start:end];text=text[:start]+text[end:]+block
  self.assertFalse(self.inspect(text)['passed'])
 def test_channel_only_in_ROM_lease(self):
  text=fixture().replace(wire.LOG,wire.channel_fixture())
  r=scm.m.inspect(text,scm_peak=True,wire=True,wire_messages=False)
  self.assertTrue(r['passed'],r);self.assertTrue(r['AP_channel_noexec_unwind_verified']);self.assertFalse(r['AP_wire_noexec_unwind_verified'])
  self.assertFalse(self.inspect(text)['passed']);self.assertFalse(scm.m.inspect(fixture(),scm_peak=True,wire=True,wire_messages=False)['passed'])
 def test_bounded_shutdown_result(self):
  record='marble-smccc: PAS_stop PASS=1 fn=42000206 arginfo=1 PAS_id=1 initial_a6=0 initial_a7=0 calls=1 outcome=0 raw_a0=0 raw_a1=0 raw_a2=0 raw_a3=0 raw_a6=0 previous_TPL=4 DAIF_before=300 DAIF_after=300 elapsed_us=500 WAITQ_replays=0 auth_reset=0 DSP_start=0'
  text=fixture().replace('marble-pil-held: shutdown ','marble-pil-progress: PAS-metadata-shutdown-before address=1\n'+record+'\nmarble-pil-progress: PAS-metadata-shutdown-return address=0\nmarble-pil-held: shutdown ')
  def inspect(t):return scm.m.inspect(t,scm_peak=True,wire=True,raw_shutdown=True)
  r=inspect(text);self.assertTrue(r['passed'],r);self.assertTrue(r['bounded_PAS_shutdown_verified'])
  self.assertFalse(self.inspect(text)['passed']);self.assertFalse(inspect(fixture())['passed']);self.assertFalse(inspect(text+'\n'+record)['passed'])
  for a,b in [('raw_a0=0','raw_a0=2'),('raw_a1=0','raw_a1=1'),('initial_a6=0','initial_a6=1'),('WAITQ_replays=0','WAITQ_replays=1'),('auth_reset=0','auth_reset=1'),('outcome=0','outcome=3'),('calls=1','calls=65')]:
   self.assertFalse(inspect(text.replace(a,b))['passed'],a)
 def test_owned_irq_ack_before_bounded_stop(self):
  record='marble-smccc: PAS_stop PASS=1 fn=42000206 arginfo=1 PAS_id=1 initial_a6=0 initial_a7=0 calls=1 outcome=0 raw_a0=0 raw_a1=0 raw_a2=0 raw_a3=0 raw_a6=0 previous_TPL=4 DAIF_before=300 DAIF_after=300 elapsed_us=500 WAITQ_replays=0 auth_reset=0 DSP_start=0'
  text=fixture().replace(wire.LOG,wire.ipcc_fixture()+'\nmarble-pil-progress: QMP-IPCC-owned-ack address=1')
  text=text.replace('marble-pil-held: shutdown ','marble-pil-progress: PAS-metadata-shutdown-before address=1\n'+record+'\nmarble-pil-progress: PAS-metadata-shutdown-return address=0\nmarble-pil-held: shutdown ')
  def inspect(t):return scm.m.inspect(t,scm_peak=True,wire=True,raw_shutdown=True,ipcc_ack=True)
  r=inspect(text);self.assertTrue(r['passed'],r);self.assertTrue(r['owned_IPCC_PAS_shutdown_verified'])
  self.assertFalse(scm.m.inspect(text,scm_peak=True,wire=True,raw_shutdown=True)['passed'])
  for a,b in [('QMP-IPCC-owned-ack address=1','QMP-IPCC-owned-ack address=0'),('observed=0','observed=30002'),('clear_writes=1','clear_writes=2'),('foreign_clears=0','foreign_clears=1')]:
   self.assertFalse(inspect(text.replace(a,b))['passed'],a)
if __name__=='__main__':unittest.main()
