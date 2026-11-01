import importlib.util,unittest
from pathlib import Path
BASE=Path(__file__).parent
def load(name,path):
 s=importlib.util.spec_from_file_location(name,path);m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m
m=load('owned',BASE.parent/'marble-adsp-owned-report.py');d=load('owned_deps',BASE/'test_marble_adsp_dependency_report.py')
smp=load('owned_smp_fixture',BASE/'test_marble_smp2p_report.py')
def fixture():
 text=d.fixture().replace('marble-adsp-dependency: begin contract=RUN-XO-LPI-bandwidth-reset-v1 DSP_start=0',
  'marble-adsp-owned: begin contract=RUN-XO-LPI-BW-ROM-noexec-reset-v1 package_bytes=30180144 package_fingerprints=2 DSP_start=0')
 for phase in ('lpi','xo','bandwidth'):text=text.replace('marble-adsp-dependency: phase='+phase,'marble-adsp-owned: phase='+phase)
 install='marble-adsp-owned: phase=install retained_watchdog=1\nmarble-pil: install_complete PASS=1 watchdog_clear=ffffffffffffffff reboot_required=1 functional_acceptance=0 watchdog_retained=1\nmarble-adsp-owned: phase=smp2p DSP_off=1\n'+smp.notify_fixture()+'\n'
 text=text.replace('marble-adsp-owned: phase=lpi',install+'marble-adsp-owned: phase=lpi')
 pil=d.n.powered_fixture();pil=pil[pil.index('marble-pil-held: loaded '):pil.index('marble-npa-held: returned ')]
 action='marble-adsp-owned: phase=ROM-noexec bandwidth_held=1 XO_held=1 LPI_clients=2 DSP_start=0\n'+pil+'marble-adsp-owned: action_return outcome=1 bandwidth_release_allowed=1\n'
 text=text.replace('marble-icb-bw: completed ',action+'marble-icb-bw: completed ')
 text=text.replace('elapsed_us=1000 watchdog_armed=1','elapsed_us=11000000 watchdog_armed=1')
 text=text.replace('elapsed_us=100500','elapsed_us=12000000').replace('limit_us=15000000 limit_ok=1','limit_us=15000000 limit_ok=1 elapsed_us=12000000')
 text=text.replace('marble-adsp-dependency: complete PASS=1','marble-adsp-owned: complete PASS=1 ROM_stage_restored=1').replace('ICB_RUN_retained=1 watchdog_armed=1','ICB_RUN_retained=1 SMEM_retained=1 watchdog_armed=1')
 return text
class Owned(unittest.TestCase):
 def test_full_noexec_lifetime(self):
  r=m.inspect(fixture());self.assertTrue(r['passed'],r);self.assertTrue(r['dependency_held_ROM_verified']);self.assertFalse(r['dsp_started']);self.assertFalse(r['audio_acceptance']);self.assertFalse(r['original_hardware_restored'])
 def test_faults(self):
  for a,b in [('ROM_stage_restored=1','ROM_stage_restored=0'),('retained_watchdog=1','retained_watchdog=0'),('bandwidth_release_allowed=1','bandwidth_release_allowed=0'),('shutdown_verified=1','shutdown_verified=0'),('original_restored=1','original_restored=0'),('XO_retained=1','XO_retained=0'),('watchdog_retained=1','watchdog_retained=0'),('elapsed_us=11000000','elapsed_us=13000000'),('DSP_start=0','DSP_start=1')]:self.assertFalse(m.inspect(fixture().replace(a,b))['passed'],a)
 def test_missing_duplicate(self):
  for line in fixture().splitlines():
   if line.startswith('marble-adsp-owned:'):
    self.assertFalse(m.inspect(fixture().replace(line+'\n',''))['passed'],line);self.assertFalse(m.inspect(fixture()+line+'\n')['passed'],line)
if __name__=='__main__':unittest.main()
