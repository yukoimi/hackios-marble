"""SCM logical peak must prove capped BCM ACKs and restoration, not throughput."""
import importlib.util,unittest
from pathlib import Path
BASE=Path(__file__).parent
s=importlib.util.spec_from_file_location('scm_bw_fixture',BASE/'test_marble_icb_run_bandwidth_report.py')
f=importlib.util.module_from_spec(s);s.loader.exec_module(f);m=f.m
def fixture():
    text=f.fixture().replace('peak_bytes_per_second=1000000','peak_bytes_per_second=4294967295000').replace('watchdog_seconds=30','watchdog_seconds=5').replace('ib=1000000','ib=4294967295000')
    held='\n'.join([
        'marble-icb-bw: peak_completions phase=held PASS=1 polls=1',
        'marble-icb-bw: peak_vote address=5001c expected=20003fff actual=60003fff PASS=1',
        'marble-icb-bw: peak_vote address=50030 expected=2000007e actual=6000007e PASS=1',
        'marble-icb-bw: peak_hold PASS=1 verified=2 saturated=1 hold_us=10000 thermal_ok=1 die_mc=41000 cpu_clock_calls=0 voltage_calls=0'])+'\n'
    text=text.replace('marble-icb-bw: completed ',held+'marble-icb-bw: completed ')
    return text.replace('marble-icb-bw: list index=0 ','marble-icb-bw: peak_completions phase=released PASS=1 polls=1\nmarble-icb-bw: list index=0 ')
class ScmPeak(unittest.TestCase):
    def inspect(self,text):return m.inspect(text,True)
    def test_peak_and_release(self):
        r=self.inspect(fixture());self.assertTrue(r['passed'],r);self.assertTrue(r['scm_peak_request_verified']);self.assertFalse(r['audio_acceptance'])
    def test_cannot_relabel_1mb_test(self):
        self.assertFalse(self.inspect(f.fixture())['passed']);self.assertFalse(m.inspect(fixture())['passed'])
    def test_limits_ack_and_ownership(self):
        for a,b in [('4294967295000','4294967295'),('watchdog_seconds=5','watchdog_seconds=30'),
                ('actual=60003fff','actual=60003ffe'),('saturated=1','saturated=0'),('verified=2','verified=1'),
                ('die_mc=41000','die_mc=55000'),('polls=1','polls=51'),('phase=released PASS=1','phase=released PASS=0'),
                ('original_cache_unchanged=1','original_cache_unchanged=0'),('cpu_clock_calls=0','cpu_clock_calls=1')]:
            self.assertFalse(self.inspect(fixture().replace(a,b))['passed'],a)
    def test_held_proof_complete_unique_and_ordered(self):
        for row in fixture().splitlines():
            if row.startswith('marble-icb-bw: peak_'):
                self.assertFalse(self.inspect(fixture().replace(row,''))['passed'],row)
                self.assertFalse(self.inspect(fixture()+'\n'+row)['passed'],row)
if __name__=='__main__':unittest.main()
