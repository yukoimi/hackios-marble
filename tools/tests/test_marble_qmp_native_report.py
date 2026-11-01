import importlib.util,unittest
from pathlib import Path
s=importlib.util.spec_from_file_location('qmp_native',Path(__file__).resolve().parents[1]/'marble-qmp-native-report.py')
m=importlib.util.module_from_spec(s);s.loader.exec_module(m)
def fixture():return '\n'.join([
 'marble-qmp-native: context phase=before transport=80000000 core=80010000 low=2 link=2 channels=1 local=0 remote=2 refs=1 terminated=1 poll_present=0',
 'marble-qmp-native: preflight qualified=1 exact_abi=1 transport=qmp remote=rpm channel=aop_apps open_attempted=0 firmware_changed=0 TX_calls=0 DSP_start=0',
 'marble-qmp-native: open_before watchdog_seconds=30 limit_us=2000000 TX_calls=0 DSP_start=0',
 'marble-qmp-native: open_return status=0 result=0 handle=81000000',
 'marble-qmp-native: connected PASS=1 events=1 invalid=0 RX=0 TX=0',
 'marble-qmp-native: close_return status=0 result=0',
 'marble-qmp-native: context phase=after transport=80000000 core=80010000 low=2 link=2 channels=1 local=1 remote=2 refs=1 terminated=1 poll_present=0',
 'marble-qmp-native: close_state descriptor=1 original_restoration=1 legacy_contract_pass=1 peer_closed=0 settled_closed=0 local=ffff0000,ffff0000 remote=ffff,ffff channels=1 native_handles_released=0 local_client_released=0',
 'marble-qmp-native: complete PASS=1 attempted=1 active=0 dirty=0 connected=1 local_down=1 remote_down=0 invalid=0 RX=0 TX=0 elapsed_us=6000 watchdog_clear=0 native_event_calls=0 peer_close_contract=0 settled_close_contract=0 original_restoration=1 raw_writes=0 TX_calls=0 NPA_calls=0 PAS_calls=0 DSP_start=0 storage_access=0'])
class QmpNativeReport(unittest.TestCase):
 def test_valid(self):
  r=m.inspect(fixture());self.assertTrue(r['passed'],r);self.assertFalse(r['audio_acceptance']);self.assertFalse(r['xo_lease_verified'])
 def test_missing_or_duplicate(self):
  for line in fixture().splitlines():
   self.assertFalse(m.inspect(fixture().replace(line,''))['passed'],line)
   self.assertFalse(m.inspect(fixture()+'\n'+line)['passed'],line)
 def test_limits_ownership_and_cleanup(self):
  for a,b in [('elapsed_us=6000','elapsed_us=2000000'),('refs=1','refs=2'),('local=1','local=3'),('watchdog_clear=0','watchdog_clear=1'),('local_down=1','local_down=0'),('handle=81000000','handle=0'),('status=0','status=1'),('dirty=0','dirty=1'),('poll_present=0','poll_present=1')]:
   self.assertFalse(m.inspect(fixture().replace(a,b))['passed'],a)
 def test_scope(self):
  for k in ('raw_writes','TX_calls','NPA_calls','PAS_calls','DSP_start','storage_access','RX','TX','invalid','remote_down'):
   self.assertFalse(m.inspect(fixture().replace(k+'=0',k+'=1'))['passed'],k)
 def test_order(self):self.assertFalse(m.inspect('\n'.join(reversed(fixture().splitlines())))['passed'])
 def test_event_dispatch_requires_listener_and_promotion_proof(self):
  text=fixture().replace('native_event_calls=0','native_event_calls=3').replace('firmware_changed=0','firmware_changed=1')
  self.assertFalse(m.inspect(text)['passed'])
  pending='marble-qmp-native: context phase=link-pending transport=80000000 core=80010000 low=1 link=1 channels=0 local=0 remote=0 refs=0 terminated=1 poll_present=0\n'
  listeners='marble-qmp-native: listeners PASS=1 total=1 matched=1 unknown=0 terminated=1 callback_calls=0\n'
  progress='marble-qmp-native: link_progress PASS=1 native_event_calls=1 table_writes=0 raw_writes=0 TX_calls=0 DSP_start=0\n'
  text=pending+listeners+text.replace('marble-qmp-native: preflight ',progress+'marble-qmp-native: preflight ')
  self.assertTrue(m.inspect(text)['passed'],m.inspect(text))
  self.assertFalse(m.inspect(text.replace('unknown=0','unknown=1'))['passed'])
 def test_clean_refusal_is_not_success(self):
  text='marble-qmp-native: refused clean=1 attempted=0 raw_writes=0 TX_calls=0 NPA_calls=0 PAS_calls=0 DSP_start=0 storage_access=0'
  r=m.inspect(text);self.assertTrue(r['clean_refusal']);self.assertFalse(r['passed'])
  self.assertFalse(m.inspect(fixture()+'\n'+text)['clean_refusal'])
 def test_peer_close_is_distinct_from_original_restoration(self):
  text=fixture().replace('phase=after transport=80000000 core=80010000 low=2 link=2 channels=1 local=1 remote=2 refs=1','phase=after transport=80000000 core=80010000 low=2 link=2 channels=0 local=0 remote=0 refs=0')
  for a,b in [('remote_down=0','remote_down=1'),('peer_close_contract=0','peer_close_contract=1'),('original_restoration=1','original_restoration=0'),('legacy_contract_pass=1','legacy_contract_pass=0'),('peer_closed=0','peer_closed=1'),('remote=ffff,ffff channels=1 native_handles_released=0','remote=ffff0000,ffff0000 channels=0 native_handles_released=1'),('local_client_released=0','local_client_released=1')]:text=text.replace(a,b)
  r=m.inspect(text,True);self.assertTrue(r['passed'],r);self.assertFalse(r['original_restoration'])
  self.assertFalse(m.inspect(text)['passed']);self.assertFalse(m.inspect(fixture(),True)['passed'])
  self.assertFalse(m.inspect(text.replace('native_handles_released=1','native_handles_released=0'),True)['passed'])
 def test_settled_hardware_transcript(self):
  text=(Path(__file__).parent/'fixtures/marble-qmp-native-settled.log').read_text()
  r=m.inspect(text,settled_close=True);self.assertTrue(r['passed'],r);self.assertTrue(r['original_restoration']);self.assertFalse(r['audio_acceptance'])
  self.assertFalse(m.inspect(text)['passed']);self.assertFalse(m.inspect(text,peer_close=True)['passed'])
  for a,b in [('client_empty=1','client_empty=0'),('stale_handle_calls=0','stale_handle_calls=1'),('rounds=1','rounds=33'),('settled_closed=1','settled_closed=0'),('local_client_released=1','local_client_released=0'),('original_restoration=1','original_restoration=0')]:
   self.assertFalse(m.inspect(text.replace(a,b),settled_close=True)['passed'],a)
  for line in text.splitlines():
   if any(x in line for x in (' settle ',' listeners ',' link_progress ',' remote_reconcile ','phase=remote-reconciled')):
    self.assertFalse(m.inspect(text.replace(line,''),settled_close=True)['passed'],line)
if __name__=='__main__':unittest.main()
