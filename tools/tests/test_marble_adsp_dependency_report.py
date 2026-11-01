import importlib.util,unittest
from pathlib import Path
BASE=Path(__file__).parent
def load(name,path):
 s=importlib.util.spec_from_file_location(name,path);m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m
m=load('dependency',BASE.parent/'marble-adsp-dependency-report.py')
r=load('dep_run',BASE/'test_marble_icb_run_report.py');b=load('dep_bw',BASE/'test_marble_icb_bandwidth_report.py')
x=load('dep_xo',BASE/'test_marble_xo_rpmh_report.py');n=load('dep_npa',BASE/'test_marble_npa_report.py')
def fixture():
 metadata=b.z.fixture().split('marble-icb-zero: binding ')[0]
 npa=n.native_fixture();before,after=npa.split('marble-npa-client: destroy index=1',1)
 bw=b.fixture();bw=bw[bw.index('marble-icb-zero: binding '):]
 bw='\n'.join(l for l in bw.splitlines() if not l.startswith('marble-icb-bw: derived_cache ')).replace('contract=derived-cache-reset-v1','contract=run-request-reset-v1')
 bw=bw.replace('bandwidth_request_released=1','bandwidth_request_released=1 mode_run=2')
 simultaneous='marble-adsp-dependency: simultaneous PASS=1 XO_ack=1 bandwidth_ib=1000000 LPI_clients=2 LPI_level=256 hold_us=10000 DSP_start=0\n'
 bw=bw.replace('marble-icb-bw: completed ',simultaneous+'marble-icb-bw: completed ')
 return ('marble-adsp-dependency: begin contract=RUN-XO-LPI-bandwidth-reset-v1 DSP_start=0\n'+metadata+r.LOG+
  'marble-adsp-dependency: phase=lpi configured_level=256\n'+before+
  'marble-npa-held: begin active=2 requests=2 watchdog_seconds=30 limit_us=15000000 DSP_start=0\n'+
  'marble-adsp-dependency: phase=xo LPI_clients=2 LPI_level=256\n'+x.LOG+
  'marble-adsp-dependency: phase=bandwidth baseline=RUN_XO_LPI_held\n'+bw+'\n'+
  'marble-npa-held: returned outcome=1 release_allowed=1 active=2 watchdog_retained=1 limit_us=15000000 limit_ok=1\n'+
  'marble-npa-client: destroy index=1'+after+'\n'+
  'marble-adsp-dependency: complete PASS=1 LPI_released=1 LPI_active=0 XO_retained=1 thermal_ok=1 ICB_RUN_retained=1 watchdog_armed=1 cold_reset_required=1 original_hardware_restored=0 DSP_start=0 storage_access=0 audio_acceptance=0\n')
class Dependencies(unittest.TestCase):
 def test_complete_dependency_lifetime(self):
  value=m.inspect(fixture());self.assertTrue(value['passed'],value);self.assertTrue(value['simultaneous_dependencies_verified']);self.assertFalse(value['audio_acceptance']);self.assertFalse(value['original_hardware_restored']);self.assertFalse(value['cleanup_verified'])
 def test_faults(self):
  for a,b in [('LPI_level=256','LPI_level=384'),('XO_retained=1','XO_retained=0'),('mode_run=2','mode_run=1'),('hold_us=10000','hold_us=0'),('limit_ok=1','limit_ok=0'),('rail_cached_corner=0','rail_cached_corner=256'),('original_cache_unchanged=1','original_cache_unchanged=0'),('thermal_ok=1','thermal_ok=0'),('DSP_start=0','DSP_start=1')]:self.assertFalse(m.inspect(fixture().replace(a,b))['passed'],a)
 def test_missing_and_duplicate(self):
  for line in fixture().splitlines():
   if line.startswith('marble-adsp-dependency:'):
    self.assertFalse(m.inspect(fixture().replace(line+'\n',''))['passed'],line);self.assertFalse(m.inspect(fixture()+line+'\n')['passed'],line)
if __name__=='__main__':unittest.main()
