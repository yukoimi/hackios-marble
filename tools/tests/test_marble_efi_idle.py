"""Reject apparent WFE success without measured waits and verified restore."""
import importlib.util
import unittest
from pathlib import Path

spec=importlib.util.spec_from_file_location('idle',Path(__file__).parents[1]/'marble-efi-idle-report.py')
idle=importlib.util.module_from_spec(spec);spec.loader.exec_module(idle)


def line(phase='restored'):
    return (f'marble-efi-idle: {phase} PASS=1 active=0 dirty=0 failed=0 saved=303 now=303 '
        'period_us=426 waits=64 wait_us=24000 max_wait_us=427 immediate=1 late=0 max_poll_gap_us=500\n')


class EFIIdleReport(unittest.TestCase):
    def inspect(self,text):return idle.inspect(line('acquired')+text)

    def test_restored_run(self):
        r=self.inspect(line());self.assertTrue(r['passed']);self.assertFalse(r['battery_temperature'])

    def test_incomplete_or_duplicate(self):
        for text in ['',line()+line(),line().replace('wait_us=24000 ','')]:
            self.assertFalse(self.inspect(text)['passed'])

    def test_false_residency_and_wakeup_gap(self):
        for a,b in [('wait_us=24000','wait_us=0'),('immediate=1','immediate=64'),
                    ('waits=64','waits=3'),('max_wait_us=427','max_wait_us=2000'),
                    ('period_us=426','period_us=10000')]:
            self.assertFalse(self.inspect(line().replace(a,b))['passed'])

    def test_fault_and_restore(self):
        for a,b in [('PASS=1','PASS=0'),('active=0','active=1'),('dirty=0','dirty=1'),
                    ('failed=0','failed=1'),('now=303','now=203fb'),('late=0','late=1')]:
            self.assertFalse(self.inspect(line().replace(a,b))['passed'])
        self.assertFalse(self.inspect(line('runtime-fault')+line())['passed'])

    def test_probe_and_missing_acquisition(self):
        self.assertTrue(idle.inspect(line('probe'),'probe')['passed'])
        self.assertFalse(idle.inspect(line('probe').replace('waits=64','waits=65'),'probe')['passed'])
        self.assertFalse(idle.inspect(line())['passed'])


if __name__=='__main__':unittest.main()
