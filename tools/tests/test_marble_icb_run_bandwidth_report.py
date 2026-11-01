import importlib.util,unittest
from pathlib import Path
BASE=Path(__file__).parent
def load(name,path):
 s=importlib.util.spec_from_file_location(name,path);m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m
m=load('run_bw',BASE.parent/'marble-icb-run-bandwidth-report.py')
r=load('run_fixture',BASE/'test_marble_icb_run_report.py');b=load('bw_fixture',BASE/'test_marble_icb_bandwidth_report.py')
def fixture():
 bw='\n'.join(line for line in b.fixture().splitlines() if not line.startswith('marble-icb-bw: derived_cache ')).replace('contract=derived-cache-reset-v1','contract=run-request-reset-v1')
 bw=bw.replace('bandwidth_request_released=1','bandwidth_request_released=1 mode_run=1')
 return 'marble-icb-run-bandwidth: begin contract=native-run-request-reset-v1\n'+r.LOG+'marble-icb-run-bandwidth: phase=bandwidth baseline=after_native_post_init original_firmware_restoration=0\n'+bw+'\n'
class Combined(unittest.TestCase):
 def test_both_required(self):
  value=m.inspect(fixture());self.assertTrue(value['passed'],value);self.assertTrue(value['bounded_bandwidth_request_verified']);self.assertFalse(value['original_firmware_restored']);self.assertFalse(value['audio_acceptance']);self.assertTrue(m.inspect('marble-efi-idle: probe\n'+fixture())['passed'])
 def test_faults(self):
  for old,new in [('state=4','state=2'),('mode_run=1','mode_run=0'),('ib=1000000','ib=0'),('original_cache_unchanged=1','original_cache_unchanged=0'),('baseline=after_native_post_init','baseline=original_firmware')]:self.assertFalse(m.inspect(fixture().replace(old,new))['passed'],old)
 def test_truncated_reordered_duplicate(self):
  self.assertFalse(m.inspect(r.LOG)['passed']);self.assertFalse(m.inspect(b.fixture())['passed']);self.assertFalse(m.inspect(fixture()+fixture())['passed'])
if __name__=='__main__':unittest.main()
