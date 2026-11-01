import importlib.util,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
def load(name,path):
 s=importlib.util.spec_from_file_location(name,path);m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m
m=load('icb_bw',ROOT/'tools/marble-icb-bandwidth-report.py')
z=load('zero_fixture',Path(__file__).with_name('test_marble_icb_zero_report.py'))
def fixture():
 text=z.fixture();lines=[]
 for line in text.splitlines():
  if line.startswith('marble-icb-zero: binding '):
   lines.extend([line,'marble-icb-bw: binding vector_offset=90 vector_rva=162c complete_offset=18 complete_rva=1474 code_ranges=12 contract=derived-cache-reset-v1']);continue
  if not line.startswith('marble-icb-zero:'):
   lines.append(line);continue
  line=line.replace('marble-icb-zero:','marble-icb-bw:').replace('explicit_requests=0','explicit_requests=1').replace('bandwidth_lease=0','bandwidth_lease=1')
  if line.startswith('marble-icb-bw: qualified '):line+=' request_type=4 request_words=8 peak_bytes_per_second=1000000'
  if line.startswith('marble-icb-bw: complete '):
   lines.append('marble-icb-bw: derived_cache address=5001c expected=20000001 actual=20000001 PASS=1 reference=before_request_native_aggregate')
   line=line.replace('dirty=0','dirty=1').replace('RPMh_cache_restored=1','RPMh_cache_restored=0').replace('watchdog_clear=0','watchdog_armed=1 cold_reset_required=1 native_history_retained=1 cache_allocations_retained=1 original_hardware_restored=0 original_cache_unchanged=1 hardware_aggregate_restored=1 added_cache_derived=1 queues_drained=1').replace('bandwidth_lease=1','bandwidth_request_released=1 audio_acceptance=0')
   line+=' complete_calls=1'
  lines.append(line)
  if line.startswith('marble-icb-bw: created '):lines.extend([
   'marble-icb-bw: requested status=0 accepted=1 ib=1000000 ab=0 owner_match=1',
   'marble-icb-bw: completed status=0 zero=1 ib=0 ab=0 owner_match=1'])
 return '\n'.join(lines)+'\n'
class Bandwidth(unittest.TestCase):
 def test_real_request_required(self):
  r=m.inspect(fixture());self.assertTrue(r['passed'],r);self.assertTrue(r['bounded_bandwidth_request_verified']);self.assertFalse(r['bandwidth_lease_verified']);self.assertFalse(r['cleanup_verified']);self.assertTrue(r['reset_safe']);self.assertFalse(r['audio_acceptance']);self.assertFalse(m.inspect(z.fixture())['passed'])
 def test_faults(self):
  for a,b in [('vector_rva=162c','vector_rva=1644'),('request_words=8','request_words=32'),('ib=1000000','ib=0'),('owner_match=1','owner_match=0'),('zero=1','zero=0'),('original_cache_unchanged=1','original_cache_unchanged=0'),('actual=20000001','actual=20000002'),('native_history_retained=1','native_history_retained=0'),('complete_calls=1','complete_calls=0')]:self.assertFalse(m.inspect(fixture().replace(a,b))['passed'],a)
 def test_missing_duplicate(self):
  for line in fixture().splitlines():
   if line.startswith('marble-icb-bw:'):
    self.assertFalse(m.inspect(fixture().replace(line+'\n',''))['passed'],line)
    self.assertFalse(m.inspect(fixture()+line+'\n')['passed'],line)
if __name__=='__main__':unittest.main()
