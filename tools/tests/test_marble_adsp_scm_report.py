"""Require capped SCM peak ACKs during the complete owned no-exec ROM lease."""
import importlib.util,unittest
from pathlib import Path
BASE=Path(__file__).parent
def load(name,file):
    s=importlib.util.spec_from_file_location(name,file);m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m
o=load('scm_owned_fixture',BASE/'test_marble_adsp_owned_report.py')
p=load('scm_peak_fixture',BASE/'test_marble_scm_bandwidth_report.py');m=o.m
q=load('scm_qmp_fixture',BASE/'test_marble_adsp_qmp_report.py')
def fixture():
    text=o.fixture().replace('LPI-BW-ROM-noexec','LPI-SCM-BW-ROM-noexec')
    text=text.replace('peak_bytes_per_second=1000000','peak_bytes_per_second=4294967295000').replace('ib=1000000','ib=4294967295000').replace('bandwidth_ib=1000000','bandwidth_ib=4294967295000').replace('mode_run=2','mode_run=3')
    source=p.fixture();begin=source.index('marble-icb-bw: peak_completions phase=held ');end=source.index('marble-icb-bw: completed ',begin)
    text=text.replace('marble-adsp-dependency: simultaneous ',source[begin:end]+'marble-adsp-dependency: simultaneous ')
    return text.replace('marble-icb-bw: list index=0 ','marble-icb-bw: peak_completions phase=released PASS=1 polls=1\nmarble-icb-bw: list index=0 ')
class OwnedScm(unittest.TestCase):
    def inspect(self,text):return m.inspect(text,scm_peak=True)
    def test_full_lifetime(self):
        r=self.inspect(fixture());self.assertTrue(r['passed'],r);self.assertTrue(r['scm_peak_held_ROM_verified']);self.assertFalse(r['audio_acceptance']);self.assertFalse(r['dsp_started'])
    def test_not_equivalent_to_old_1mb(self):
        self.assertFalse(self.inspect(o.fixture())['passed']);self.assertFalse(m.inspect(fixture())['passed'])
    def test_requires_all_leases_and_restore(self):
        for a,b in [('mode_run=3','mode_run=2'),('ROM_stage_restored=1','ROM_stage_restored=0'),
                ('peak_hold PASS=1','peak_hold PASS=0'),('bandwidth_ib=4294967295000','bandwidth_ib=1000000'),
                ('watchdog_seconds=30','watchdog_seconds=5'),('XO_ack=1','XO_ack=0')]:
            self.assertFalse(self.inspect(fixture().replace(a,b))['passed'],a)
    def test_peak_plus_qmp_noexec_unwind(self):
        text=q.fixture().replace('LPI-BW-ROM-noexec','LPI-SCM-BW-ROM-noexec')
        text=text.replace('peak_bytes_per_second=1000000','peak_bytes_per_second=4294967295000').replace('ib=1000000','ib=4294967295000').replace('mode_run=2','mode_run=3')
        source=p.fixture();begin=source.index('marble-icb-bw: peak_completions phase=held ');end=source.index('marble-icb-bw: completed ',begin)
        text=text.replace('marble-adsp-dependency: simultaneous ',source[begin:end]+'marble-adsp-dependency: simultaneous ')
        text=text.replace('marble-icb-bw: list index=0 ','marble-icb-bw: peak_completions phase=released PASS=1 polls=1\nmarble-icb-bw: list index=0 ')
        r=m.inspect(text,qmp=True,scm_peak=True);self.assertTrue(r['passed'],r)
        self.assertTrue(r['qmp_noexec_unwind_verified']);self.assertTrue(r['scm_peak_held_ROM_verified'])
        self.assertFalse(m.inspect(text,scm_peak=True)['passed']);self.assertFalse(m.inspect(text,qmp=True)['passed'])
if __name__=='__main__':unittest.main()
