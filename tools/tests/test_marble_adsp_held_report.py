"""A stopped DSP/PAS must precede load_state OFF and restoration of its RAM."""
import importlib.util,unittest
from pathlib import Path
BASE=Path(__file__).parent
def load(name,file):
 s=importlib.util.spec_from_file_location(name,BASE/file);m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m
scm=load('held_scm_fixture','test_marble_adsp_scm_report.py')
wire=load('held_wire_fixture','test_marble_qmp_wire_report.py')
RAW='marble-smccc: PAS_stop PASS=1 fn=42000206 arginfo=1 PAS_id=1 initial_a6=0 initial_a7=0 calls=1 outcome=0 raw_a0=0 raw_a1=0 raw_a2=0 raw_a3=0 raw_a6=0 previous_TPL=4 DAIF_before=300 DAIF_after=300 elapsed_us=500 WAITQ_replays=0 auth_reset=0 DSP_start=0'
def fixture():
 text=scm.fixture();pre,post=wire.LOG.split('marble-qmp-wire: send_before index=1 ',1)
 qualified='marble-qmp-ipcc: qualified APPS_client=8 source_client=0 signal=0 base=ed18000 receive_offset=10 clear_offset=1c code_ranges=2 before=ffffffff IRQ_enable_writes=0 GIC_writes=0 native_callbacks=0'
 before='\n'.join([qualified,pre.rstrip(),
  'marble-qmp-hold: notify stage=1 PASS=1 observed=0 after=ffffffff clear_writes=1 foreign_clears=0 IRQ_enable_writes=0 GIC_writes=0',
  'marble-qmp-hold: on PASS=1 mailbox_ACKs=1 IPCC_clears=1 channel_open=1 PAS_stop_calls=0 DSP_start=0',
  'marble-pil-progress: QMP-held-on-return address=1',
  'marble-pil-held: post_load_gate PASS=1 ROM_loaded=1 PAS_initialized=1 DSP_start=0',
  'marble-pil-progress: PAS-metadata-shutdown-before address=1',
  'marble-qmp-hold: stop PASS=1 on_held=1 mailbox_ACKs=1 IPCC_clears=1 DSP_start=0',RAW,
  'marble-pil-progress: PAS-metadata-shutdown-return address=0'])+'\n'
 after='\n'.join(['marble-qmp-wire: send_before index=1 '+post,
  'marble-qmp-hold: notify stage=2 PASS=1 observed=0 after=ffffffff clear_writes=2 foreign_clears=0 IRQ_enable_writes=0 GIC_writes=0',
  'marble-qmp-hold: complete PASS=1 ON_before_PAS=1 PAS_before_OFF=1 mailbox_ACKs=2 IPCC_clears=2 wire_preserved=1 DSP_start=0 audio_acceptance=0',
  'marble-pil-held: post_shutdown_gate PASS=1 PAS_stopped=1 payload_restore=0'])+'\n'
 text=text.replace('marble-pil-held: shutdown ',before+'marble-pil-held: shutdown ',1)
 return text.replace('marble-pil-held: restored ',after+'marble-pil-held: restored ',1)
class HeldLifecycle(unittest.TestCase):
 def inspect(self,text):return scm.m.inspect(text,scm_peak=True,wire=True,raw_shutdown=True,held=True)
 def test_ordered_complete_lifecycle(self):
  r=self.inspect(fixture());self.assertTrue(r['passed'],r);self.assertTrue(r['load_state_held_through_shutdown_verified']);self.assertFalse(r['audio_acceptance']);self.assertFalse(r['dsp_started'])
 def test_not_implicitly_allowed_by_old_contract(self):
  self.assertFalse(scm.m.inspect(fixture(),scm_peak=True,wire=True,raw_shutdown=True)['passed'])
  self.assertFalse(self.inspect(scm.fixture())['passed'])
 def test_bad_stop_or_owned_ack_prevents_success(self):
  for a,b in [('on_held=1','on_held=0'),('raw_a0=0','raw_a0=1'),('PAS_before_OFF=1','PAS_before_OFF=0'),
    ('clear_writes=2','clear_writes=1'),('foreign_clears=0','foreign_clears=1'),('channel_open=1','channel_open=0'),
    ('post_shutdown_gate PASS=1','post_shutdown_gate PASS=0'),('wire_preserved=1','wire_preserved=0'),
    ('stage=2 PASS=1','stage=2 PASS=0'),('observed=0','observed=30000')]:
   self.assertFalse(self.inspect(fixture().replace(a,b))['passed'],a)
 def test_early_off_rejected(self):
  text=fixture();start=text.index('marble-qmp-wire: send_before index=1 ');end=text.index('marble-pil-held: restored ',start)
  off=text[start:end];text=text[:start]+text[end:]
  pos=text.index('marble-pil-progress: PAS-metadata-shutdown-before')
  self.assertFalse(self.inspect(text[:pos]+off+text[pos:])['passed'])
 def test_missing_or_duplicate_hold_evidence(self):
  text=fixture()
  for line in text.splitlines():
   if line.startswith(('marble-qmp-hold:','marble-pil-held: post_shutdown_gate ')):
    self.assertFalse(self.inspect(text.replace(line,''))['passed'],line)
    self.assertFalse(self.inspect(text+'\n'+line)['passed'],line)
if __name__=='__main__':unittest.main()
