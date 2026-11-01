import importlib.util,unittest
from pathlib import Path
s=importlib.util.spec_from_file_location('rpmh_native',Path(__file__).resolve().parents[1]/'marble-rpmh-native-report.py')
m=importlib.util.module_from_spec(s);s.loader.exec_module(m)
def fixture():return '\n'.join([
 'marble-rpmh-native: qualified exact=1 slots=24 code_words=382 drv=2 hardware_drv=2 commands=16 Init_calls=0',
 'marble-rpmh-native: inventory stable=1 resources=2 votes=2 active=1 pool=0 pending_issue_calls=0',
 'marble-rpmh-native: queries valid_status=0 valid=1 num_status=0 commands=16 find_status=0 lookup_match=1 xo_cached=0 issue_calls=0',
 'marble-rpmh-native: created status=0 captured=1 appended=1 empty=1 resources_unchanged=1 active=2 pool=0',
 'marble-rpmh-native: released status=0 snapshot=1 clients_restored=1 resources_restored=1 native_pool_retained=1 active=1 pool=1 elapsed_us=1000',
 'marble-rpmh-native: complete PASS=1 attempted=1 dirty=0 create_calls=1 delete_calls=1 query_calls=3 watchdog_clear=0 issue_calls=0 votes=0 raw_writes=0 NPA_calls=0 QMP_calls=0 PAS_calls=0 DSP_start=0 storage_access=0 lease_accepted=0'])
class RpmhNative(unittest.TestCase):
 def test_real_hardware_fixture(self):
  text=(Path(__file__).parent/'fixtures/marble-rpmh-native-zero.log').read_text()
  self.assertTrue(m.inspect(text)['passed']);self.assertFalse(m.inspect(text.replace('active=6','active=7'))['passed'])
 def test_zero_commands_is_not_lease(self):
  r=m.inspect(fixture());self.assertTrue(r['passed'],r);self.assertFalse(r['resource_leases_verified']);self.assertFalse(r['audio_acceptance'])
 def test_missing_duplicate_order(self):
  for line in fixture().splitlines():
   self.assertFalse(m.inspect(fixture().replace(line,''))['passed']);self.assertFalse(m.inspect(fixture()+'\n'+line)['passed'])
  self.assertFalse(m.inspect('\n'.join(reversed(fixture().splitlines())))['passed'])
 def test_closure_and_limits(self):
  for a,b in [('active=2','active=3'),('elapsed_us=1000','elapsed_us=1000000'),('clients_restored=1','clients_restored=0'),('resources_restored=1','resources_restored=0'),('native_pool_retained=1','native_pool_retained=0'),('empty=1','empty=0'),('watchdog_clear=0','watchdog_clear=1'),('code_words=382','code_words=381')]:
   self.assertFalse(m.inspect(fixture().replace(a,b))['passed'],a)
 def test_no_business_calls(self):
  for key in ('Init_calls','issue_calls','raw_writes','NPA_calls','QMP_calls','PAS_calls','DSP_start','storage_access','lease_accepted'):
   self.assertFalse(m.inspect(fixture().replace(key+'=0',key+'=1'))['passed'],key)
 def test_refusal_is_not_success(self):
  text='marble-rpmh-native: refused clean=1 attempted=0 dirty=0'
  r=m.inspect(text);self.assertTrue(r['clean_refusal']);self.assertFalse(r['passed']);self.assertFalse(m.inspect(fixture()+'\n'+text)['clean_refusal'])
if __name__=='__main__':unittest.main()
