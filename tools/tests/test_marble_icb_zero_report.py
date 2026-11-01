import importlib.util,unittest
from pathlib import Path
s=importlib.util.spec_from_file_location('icb_zero',Path(__file__).resolve().parents[1]/'marble-icb-zero-report.py')
m=importlib.util.module_from_spec(s);s.loader.exec_module(m)
def fixture():
    lines=[(Path(__file__).parent/'fixtures/marble-adsp-dependencies.log').read_text().rstrip(),
        'marble-icb-zero: binding create_offset=10 create_rva=1448 destroy_offset=78 destroy_rva=15e4 explicit=1',
        'marble-icb-zero: qualified exact_abi=1 route_names=8 hops=4 vector_type=400 pair_bytes=8 explicit_requests=0 watchdog_seconds=30',
        'marble-icb-zero: created status=0 owned=1 empty=1 lists_appended=1 clients_before=1 clients_during=2']
    for i in range(8):lines.append(f'marble-icb-zero: list index={i} restored=1 before=0 after=0 old_capacity=0 native_capacity=10')
    lines.append('marble-icb-zero: complete PASS=1 destroy_status=0 attempted=1 dirty=0 lists_restored=1 clients_restored=1 resource_restored=1 RPMh_cache_restored=1 elapsed_us=1000 watchdog_clear=0 create_calls=1 destroy_calls=1 explicit_requests=0 DSP_start=0 storage_access=0 bandwidth_lease=0')
    return '\n'.join(lines)
class IcbZero(unittest.TestCase):
 def test_zero_request_is_not_bandwidth_lease(self):
  r=m.inspect(fixture());self.assertTrue(r['passed'],r);self.assertFalse(r['bandwidth_lease_verified']);self.assertFalse(r['audio_acceptance'])
 def test_missing_duplicate_lifecycle(self):
  text=fixture()
  for line in text.splitlines():
   if line.startswith('marble-icb-zero:'):
    self.assertFalse(m.inspect(text.replace(line,''))['passed']);self.assertFalse(m.inspect(text+'\n'+line)['passed'])
 def test_state_and_limits(self):
  for a,b in [('destroy_offset=78','destroy_offset=70'),('destroy_rva=15e4','destroy_rva=15b8'),('owned=1','owned=0'),('empty=1','empty=0'),('before=0 after=0','before=0 after=1'),('native_capacity=10','native_capacity=11'),('elapsed_us=1000','elapsed_us=1000000'),('RPMh_cache_restored=1','RPMh_cache_restored=0'),('clients_during=2','clients_during=3'),('vector_type=400','vector_type=100'),('name=qxm_crypto','name=other')]:
   self.assertFalse(m.inspect(fixture().replace(a,b))['passed'],a)
 def test_scope(self):
  for k in ('explicit_requests','DSP_start','storage_access','bandwidth_lease'):
   self.assertFalse(m.inspect(fixture().replace(k+'=0',k+'=1'))['passed'],k)
 def test_refusal_is_not_cleanup(self):
  text='marble-icb-zero: refused clean=1 attempted=0 dirty=0'
  r=m.inspect(text);self.assertTrue(r['clean_refusal']);self.assertFalse(r['passed']);self.assertFalse(m.inspect(fixture()+'\n'+text)['clean_refusal'])
if __name__=='__main__':unittest.main()
