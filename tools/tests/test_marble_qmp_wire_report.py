"""Reject stale/partial wire ACKs and changes to the firmware owner context."""
import importlib.util,unittest
from pathlib import Path
s=importlib.util.spec_from_file_location('wire_report',Path(__file__).resolve().parents[1]/'marble-qmp-wire-report.py')
m=importlib.util.module_from_spec(s);s.loader.exec_module(m)
LOG='\n'.join([
 'marble-qmp-wire: qualified native_code_ranges=13 features=0 local_offset=128 local_bytes=728 remote_offset=c0 remote_bytes=104 native_low=1 native_core=1 native_channels=0 Clock_flags=0 DAIF=300 GLink_calls=0',
 'marble-qmp-wire: opened PASS=1 native_context_unchanged=1',
 'marble-qmp-wire: send_before index=0 value=on bytes=64 AP_mailbox=c300128',
 'marble-qmp-wire: send_complete index=0 PASS=1 published=64 ACKs=1 elapsed_us=1100 physical_readback=0 GLink_TX_calls=0',
 'marble-qmp-wire: send_before index=1 value=off bytes=64 AP_mailbox=c300128',
 'marble-qmp-wire: send_complete index=1 PASS=1 published=64 ACKs=2 elapsed_us=1100 physical_readback=0 GLink_TX_calls=0',
 'marble-qmp-wire: close_ack index=0 observed_remote=ffff0000 AP_ack=ffff0000 offset=18 remote_state_writes=0',
 'marble-qmp-wire: close_ack index=1 observed_remote=ffff AP_ack=ffff offset=18 remote_state_writes=0',
 'marble-qmp-wire: context native_transport_words=38 native_core_words=19 Clock_before=0 Clock_after=0 DAIF_before=300 DAIF_after=300',
 'marble-qmp-wire: complete PASS=1 attempted=1 dirty=0 descriptor_restored=1 native_context_unchanged=1 Clock_unchanged=1 DAIF_unchanged=1 AP_writes=38 kicks=6 ACKs=2 elapsed_us=114000 thermal_ok=1 watchdog_seconds=10 watchdog_retained=1 cold_reset_required=1 GLink_calls=0 native_event_calls=0 remote_owned_writes=0 table_writes=0 PAS_calls=0 NPA_calls=0 DSP_start=0 storage_access=0 AP_payload_history_retained=1 physical_load_state_readback=0'])
class Wire(unittest.TestCase):
 def test_full_borrow_and_return(self):
  r=m.inspect(LOG);self.assertTrue(r['passed'],r);self.assertFalse(r['physical_load_state_readback']);self.assertFalse(r['audio_acceptance'])
 def test_missing_or_duplicate(self):
  for line in LOG.splitlines():
   self.assertFalse(m.inspect(LOG.replace(line,''))['passed'],line);self.assertFalse(m.inspect(LOG+'\n'+line)['passed'],line)
 def test_state_changes_and_boundaries(self):
  for a,b in [('native_context_unchanged=1','native_context_unchanged=0'),('Clock_after=0','Clock_after=1'),('DAIF_after=300','DAIF_after=3c0'),
    ('local_offset=128','local_offset=c0'),('local_bytes=728','local_bytes=729'),('AP_mailbox=c300128','AP_mailbox=c3000c0'),('published=64','published=0'),
    ('AP_writes=38','AP_writes=36'),('ACKs=2','ACKs=1'),('watchdog_retained=1','watchdog_retained=0'),('elapsed_us=1100','elapsed_us=250000'),
    ('elapsed_us=114000','elapsed_us=2500000'),('native_core_words=19','native_core_words=18'),('offset=18','offset=14'),('AP_ack=ffff0000','AP_ack=ffff'),('remote_state_writes=0','remote_state_writes=1')]:self.assertFalse(m.inspect(LOG.replace(a,b))['passed'],a)
 def test_no_other_hardware_actions(self):
  for key in ('GLink_calls','native_event_calls','remote_owned_writes','table_writes','PAS_calls','NPA_calls','DSP_start','storage_access','physical_load_state_readback'):
   self.assertFalse(m.inspect(LOG.replace(key+'=0',key+'=1'))['passed'],key)
 def test_clean_refusal(self):
  text='marble-qmp-wire: refused clean=1 AP_writes=0 GLink_calls=0 PAS_calls=0 DSP_start=0'
  self.assertTrue(m.inspect(text)['clean_refusal']);self.assertFalse(m.inspect(text)['passed']);self.assertFalse(m.inspect(LOG+'\n'+text)['clean_refusal'])
 def test_channel_isolation(self):
  text=channel_fixture();r=m.inspect(text,messages=False)
  self.assertTrue(r['passed'],r);self.assertTrue(r['AP_channel_handshake_verified']);self.assertFalse(r['AP_wire_transfer_verified'])
  self.assertFalse(m.inspect(text)['passed']);self.assertFalse(m.inspect(LOG,messages=False)['passed'])
  for a,b in [('load_state_messages=0','load_state_messages=2'),('AP_writes=4','AP_writes=38'),('ACKs=0','ACKs=2'),('owned=1','owned=0'),('AP_payload_history_retained=0','AP_payload_history_retained=1')]:
   self.assertFalse(m.inspect(text.replace(a,b),messages=False)['passed'],a)
 def test_tpl_boundary_brackets_wire(self):
  def trip(i):return 'marble-qmp-tpl: roundtrip index=%d PASS=1 requested=8 previous=4 DAIF_before=300 DAIF_after=300 elapsed_us=50 raises=1 restores=1 direct_DAIF_writes=0 SMC=0 PAS=0'%i
  finish='marble-qmp-tpl: complete PASS=1 baseline=1 wire=1 post=1 firmware_preserved=1 raises=2 restores=2 SMC=0 PAS=0 NPA=0 DSP_start=0 watchdog_retained=1 cold_reset_required=1'
  text=trip(0)+'\n'+LOG+'\n'+trip(1)+'\n'+finish
  r=m.inspect(text,tpl=True);self.assertTrue(r['passed'],r);self.assertTrue(r['TPL_roundtrip_verified'])
  for a,b in [('previous=4','previous=16'),('restores=2','restores=1'),('post=1','post=0'),('DAIF_after=300','DAIF_after=3c0'),('elapsed_us=50','elapsed_us=250000')]:
   self.assertFalse(m.inspect(text.replace(a,b),tpl=True)['passed'],a)
  self.assertFalse(m.inspect(LOG,tpl=True)['passed']);self.assertFalse(m.inspect(text+'\n'+trip(1),tpl=True)['passed'])
  self.assertFalse(m.inspect(LOG+'\n'+trip(0)+'\n'+trip(1)+'\n'+finish,tpl=True)['passed'])
 def test_bounded_smccc_queries(self):
  def query(i):return 'marble-smccc: query index=%d PASS=1 fn=%s arginfo=1 target=2000206 initial_a6=0 initial_a7=0 calls=1 outcome=0 raw_a0=0 raw_a1=1 raw_a2=0 raw_a3=0 raw_a6=0 previous_TPL=4 DAIF_before=300 DAIF_after=300 elapsed_us=50 WAITQ_replays=0 PAS_calls=0 DSP_start=0'%(i,'42000601' if i&1 else 'c2000601')
  finish='marble-smccc: complete PASS=1 baseline=1 wire=1 post=1 firmware_preserved=1 availability_queries=4 WAITQ_replays=0 PAS_calls=0 NPA_calls=0 DSP_start=0 watchdog_retained=1 cold_reset_required=1'
  text='\n'.join([query(0),query(1),LOG,query(2),query(3),finish]);r=m.inspect(text,smccc=True)
  self.assertTrue(r['passed'],r);self.assertTrue(r['SMCCC_availability_verified'])
  for a,b in [('raw_a0=0','raw_a0=2'),('initial_a6=0','initial_a6=1'),('initial_a7=0','initial_a7=1'),('WAITQ_replays=0','WAITQ_replays=1'),('calls=1','calls=65'),('fn=42000601','fn=42000206'),('outcome=0','outcome=3')]:
   self.assertFalse(m.inspect(text.replace(a,b),smccc=True)['passed'],a)
  self.assertFalse(m.inspect(LOG,smccc=True)['passed']);self.assertFalse(m.inspect(text+'\n'+query(3),smccc=True)['passed'])
 def test_only_new_owned_ipcc_notification(self):
  before='marble-qmp-ipcc: qualified APPS_client=8 source_client=0 signal=0 base=ed18000 receive_offset=10 clear_offset=1c code_ranges=2 before=ffffffff IRQ_enable_writes=0 GIC_writes=0 native_callbacks=0'
  after='marble-qmp-ipcc: complete PASS=1 before=ffffffff observed=0 after=ffffffff owned=1 clear_writes=1 firmware_preserved=1 IRQ_enable_writes=0 GIC_writes=0 native_callbacks=0 foreign_clears=0 PAS_calls=0 DSP_start=0 watchdog_retained=1 cold_reset_required=1 original_irq_state_restored=1'
  text=before+'\n'+LOG+'\n'+after;r=m.inspect(text,ipcc=True)
  self.assertTrue(r['passed'],r);self.assertTrue(r['owned_IPCC_acknowledgement_verified'])
  for a,b in [('before=ffffffff','before=0'),('observed=0','observed=30002'),('clear_writes=1','clear_writes=2'),('GIC_writes=0','GIC_writes=1'),('foreign_clears=0','foreign_clears=1'),('after=ffffffff','after=0')]:
   self.assertFalse(m.inspect(text.replace(a,b),ipcc=True)['passed'],a)
  self.assertFalse(m.inspect(LOG,ipcc=True)['passed']);self.assertFalse(m.inspect(text+'\n'+after,ipcc=True)['passed'])
def ipcc_fixture():
 before='marble-qmp-ipcc: qualified APPS_client=8 source_client=0 signal=0 base=ed18000 receive_offset=10 clear_offset=1c code_ranges=2 before=ffffffff IRQ_enable_writes=0 GIC_writes=0 native_callbacks=0'
 after='marble-qmp-ipcc: complete PASS=1 before=ffffffff observed=0 after=ffffffff owned=1 clear_writes=1 firmware_preserved=1 IRQ_enable_writes=0 GIC_writes=0 native_callbacks=0 foreign_clears=0 PAS_calls=0 DSP_start=0 watchdog_retained=1 cold_reset_required=1 original_irq_state_restored=1'
 return before+'\n'+LOG+'\n'+after
def channel_fixture():
 text='\n'.join(l for l in LOG.splitlines() if not l.startswith(('marble-qmp-wire: send_before ','marble-qmp-wire: send_complete ')))
 return 'marble-qmp-wire: mode load_state_messages=0 owned=1 PAS_inside_wire=0\n'+text.replace('AP_writes=38 kicks=6 ACKs=2','AP_writes=4 kicks=4 ACKs=0').replace('AP_payload_history_retained=1','AP_payload_history_retained=0')
if __name__=='__main__':unittest.main()
