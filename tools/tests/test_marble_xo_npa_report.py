import importlib.util,unittest
from pathlib import Path
s=importlib.util.spec_from_file_location('xo_npa',Path(__file__).resolve().parents[1]/'marble-xo-npa-report.py')
m=importlib.util.module_from_spec(s);s.loader.exec_module(m)
def fixture():
 lines=['marble-xo-npa: binding define_offset=20 define_rva=148c scalar_offset=28 scalar_rva=14a4 destroy_offset=78 code_ranges=4',
 'marble-xo-npa: defined status=0 ready=1 driver_calls=1 name=/marble/clock/xo-test resource_retained=1 hardware_calls=0']
 for i in range(3):lines.append(f'marble-xo-npa: client index={i} status=0 created=1')
 for i,(a,r,s) in enumerate([(1,1,0),(1,1,0),(1,1,0),(1,1,1),(1,0,1),(0,0,0)]):lines.append(f'marble-xo-npa: request index={i} status=0 PASS=1 active={a} required={r} suppressible={s}')
 for i in (2,1,0):lines.append(f'marble-xo-npa: destroy index={i} status=0')
 lines.append('marble-xo-npa: complete PASS=1 attempted=1 dirty=0 clients_released=1 RPMh_cache_restored=1 elapsed_us=1000 watchdog_clear=0 provider_retained=1 graph_restored=0 cold_reset_required=1 hardware_calls=0 DSP_start=0 storage_access=0 lease_accepted=0')
 return '\n'.join(lines)
class XoNpa(unittest.TestCase):
 def test_software_is_not_physical_clock(self):
  r=m.inspect(fixture());self.assertTrue(r['passed'],r);self.assertFalse(r['physical_xo_lease_verified'])
 def test_missing_duplicate_order(self):
  text=fixture()
  for line in text.splitlines():
   self.assertFalse(m.inspect(text.replace(line,''))['passed']);self.assertFalse(m.inspect(text+'\n'+line)['passed'])
  self.assertFalse(m.inspect('\n'.join(reversed(text.splitlines())))['passed'])
 def test_boundaries_and_scope(self):
  for a,b in [('required=1 suppressible=0','required=0 suppressible=0'),('destroy_offset=78','destroy_offset=70'),('hardware_calls=0','hardware_calls=1'),('elapsed_us=1000','elapsed_us=1000000'),('graph_restored=0','graph_restored=1'),('RPMh_cache_restored=1','RPMh_cache_restored=0'),('watchdog_clear=0','watchdog_clear=1'),('lease_accepted=0','lease_accepted=1')]:
   self.assertFalse(m.inspect(fixture().replace(a,b))['passed'],a)
 def test_clean_refusal_is_not_pass(self):
  r=m.inspect('marble-xo-npa: refused clean=1 attempted=0 dirty=0');self.assertTrue(r['clean_refusal']);self.assertFalse(r['passed'])
if __name__=='__main__':unittest.main()
