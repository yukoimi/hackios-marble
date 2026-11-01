import importlib.util,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
s=importlib.util.spec_from_file_location('xo_report',ROOT/'tools/marble-xo-rpmh-report.py');m=importlib.util.module_from_spec(s);s.loader.exec_module(m)
LOG='''marble-xo-rpmh: binding create_offset=30 create_rva=16ec issue_offset=98 issue_rva=1890 churn_offset=58 churn_rva=17b4 code_ranges=12
marble-xo-rpmh: inventory stable=1 xo_cached=0 original_resources=32 original_votes=32 original_clients=5 original_hardware_vote_known=0
marble-xo-rpmh: client status=0 owned_empty=1
marble-xo-rpmh: issue index=0 set=1 status=0 data=3 request=0
marble-xo-rpmh: issue index=1 set=2 status=0 data=3 request=0
marble-xo-rpmh: issue index=2 set=0 status=0 data=3 request=1
marble-xo-rpmh: held PASS=1 churn_status=0 original_owners_unchanged=1 xo_vote_acknowledged=1 queues_drained=1 thermal_ok=1 elapsed_us=12000 issue_calls=3 zero_vote_calls=0 raw_writes=0 retained_client=1 dirty=1 watchdog_armed=1 cold_reset_required=1 physical_release_verified=0 original_hardware_restored=0 DSP_start=0 storage_access=0 audio_acceptance=0
'''
class Report(unittest.TestCase):
 def test_hold_is_not_release_or_audio(self):
  r=m.inspect(LOG);self.assertTrue(r['passed'],r);self.assertFalse(r['physical_release_verified']);self.assertFalse(r['audio_acceptance'])
 def test_mutations(self):
  for before,after in [('churn_rva=17b4','churn_rva=17e4'),('xo_cached=0','xo_cached=1'),('request=1','request=0'),('data=3','data=0'),('queues_drained=1','queues_drained=0'),('original_owners_unchanged=1','original_owners_unchanged=0'),('physical_release_verified=0','physical_release_verified=1'),('watchdog_armed=1','watchdog_armed=0'),('elapsed_us=12000','elapsed_us=1000000')]:
   self.assertFalse(m.inspect(LOG.replace(before,after))['passed'],before)
 def test_clean_refusal(self):
  self.assertTrue(m.inspect('marble-xo-rpmh: refused clean=1 attempted=0 dirty=0\n')['clean_refusal'])
  self.assertFalse(m.inspect(LOG+'marble-xo-rpmh: refused clean=1 attempted=0 dirty=0\n')['clean_refusal'])
 def test_reorder_duplicate_truncation(self):
  self.assertFalse(m.inspect(LOG+LOG)['passed'])
  self.assertFalse(m.inspect('\n'.join(reversed(LOG.splitlines())))['passed'])
  for line in LOG.splitlines():self.assertFalse(m.inspect(LOG.replace(line+'\n',''))['passed'])
if __name__=='__main__':unittest.main()
