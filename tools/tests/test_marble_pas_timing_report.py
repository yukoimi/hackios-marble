import importlib.util,unittest
from pathlib import Path
s=importlib.util.spec_from_file_location('pas_timing',Path(__file__).parents[1]/'marble-pas-timing-report.py')
m=importlib.util.module_from_spec(s);s.loader.exec_module(m)
def fixture(total=608000):
 return (f'aud-pas-time: index=0 smc_us=300000 thermal_us=10 other_us=1 raw_a0=1\n'
         f'aud-pas-time: index=1 smc_us={total-8000} thermal_us=0 other_us=2 raw_a0=0\n').replace(f'smc_us={total-8000}',f'smc_us={total-308000}')+(
         f'aud-pas-timing: version=1 calls=2 total_us={total} smc_us={total-8000} thermal_us=10 other_us=3 max_smc_us={max(300000,total-308000)} limit_us=1000000 limit_calls=128 IRQ_masks_unchanged=1 USB_inside_calls=0 audio_acceptance=0\n'
         f'marble-smccc: PAS_stop PASS={int(total<1000000)} calls=2 elapsed_us={total} raw_a0=0\n')
class Timing(unittest.TestCase):
 def test_version_two_accounts_tpl_delay_without_granting_pas_success(self):
  text=fixture().replace('version=1','version=2').replace('total_us=608000','total_us=1408000').replace('elapsed_us=608000','elapsed_us=1408000').replace('PAS_stop PASS=1','PAS_stop PASS=0')
  text=text.replace('limit_us=1000000','entry_us=0 raise_us=0 post_us=0 restore_us=800000 limit_us=1000000')
  r=m.inspect(text);self.assertTrue(r['passed'],r);self.assertFalse(r['PAS_acceptance'])
  for a,b in [('restore_us=800000','restore_us=0'),('entry_us=0','entry_us=-1'),('restore_us=800000','restore_us=1600000')]:
   self.assertFalse(m.inspect(text.replace(a,b))['passed'])
 def test_consistent_timings_do_not_grant_shutdown_acceptance(self):
  for total in (608000,1408000):
   r=m.inspect(fixture(total));self.assertTrue(r['passed'],r);self.assertFalse(r['PAS_acceptance']);self.assertFalse(r['audio_acceptance'])
 def test_missing_corrupt_changed_budget_or_excessive_gap_rejected(self):
  text=fixture()
  for a,b in [('index=1','index=0'),('calls=2','calls=3'),('thermal_us=10','thermal_us=20'),
              ('limit_us=1000000','limit_us=2000000'),('raw_a0=1','raw_a0=0'),('max_smc_us=300000','max_smc_us=1'),
              ('USB_inside_calls=0','USB_inside_calls=1'),('total_us=608000','total_us=1608000')]:
   self.assertNotEqual(text,text.replace(a,b));self.assertFalse(m.inspect(text.replace(a,b,1))['passed'],a)
  for line in text.splitlines():
   self.assertFalse(m.inspect(text.replace(line+'\n',''))['passed']);self.assertFalse(m.inspect(text+line+'\n')['passed'])
if __name__=='__main__':unittest.main()
