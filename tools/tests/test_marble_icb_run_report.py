import importlib.util,unittest
from pathlib import Path
s=importlib.util.spec_from_file_location('icb_run',Path(__file__).resolve().parents[1]/'marble-icb-run-report.py');m=importlib.util.module_from_spec(s);s.loader.exec_module(m)
LOG='''marble-icb-run: binding protocol_offset=8 protocol_rva=13f4 implementation_rva=2aa4 post_init_rva=4088 finish_offset=68 finish_rva=17e4 code_ranges=10
marble-icb-run: before state=2 hardware_nodes=24 standalone_nodes=0 rpmh_resources=32 rpmh_clients=5 direct_state_writes=0
marble-icb-run: complete PASS=1 status=0 state=4 rpmh_snapshot=1 completion_polls=2 queues_drained=1 NPA_clients_unchanged=1 hardware_aggregates_unchanged=1 thermal_ok=1 elapsed_us=12000 watchdog_armed=1 cold_reset_required=1 firmware_RUN_retained=1 original_hardware_restored=0 direct_state_writes=0 explicit_bandwidth_requests=0 DSP_start=0 storage_access=0 audio_acceptance=0
'''
class Run(unittest.TestCase):
 def test_transition_is_not_audio_or_local_restore(self):
  r=m.inspect(LOG);self.assertTrue(r['passed'],r);self.assertTrue(r['reset_safe']);self.assertFalse(r['cleanup_verified']);self.assertFalse(r['audio_acceptance'])
 def test_faults(self):
  for a,b in [('protocol_rva=13f4','protocol_rva=140c'),('finish_rva=17e4','finish_rva=17b4'),('state=4','state=2'),('queues_drained=1','queues_drained=0'),('direct_state_writes=0','direct_state_writes=1'),('thermal_ok=1','thermal_ok=0'),('completion_polls=2','completion_polls=501'),('standalone_nodes=0','standalone_nodes=2'),('elapsed_us=12000','elapsed_us=1000000'),('original_hardware_restored=0','original_hardware_restored=1')]:self.assertFalse(m.inspect(LOG.replace(a,b))['passed'],a)
 def test_missing_duplicate_refusal(self):
  for line in LOG.splitlines():
   self.assertFalse(m.inspect(LOG.replace(line+'\n',''))['passed']);self.assertFalse(m.inspect(LOG+line+'\n')['passed'])
  self.assertTrue(m.inspect('marble-icb-run: refused clean=1 attempted=0 dirty=0')['clean_refusal'])
  self.assertFalse(m.inspect(LOG+'marble-icb-run: refused clean=1 attempted=0 dirty=0')['passed'])
if __name__=='__main__':unittest.main()
