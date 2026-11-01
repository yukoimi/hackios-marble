"""A temperature log must not turn failed/partial/hot runs into acceptance."""
import importlib.util, tempfile, unittest
from pathlib import Path

spec=importlib.util.spec_from_file_location('thermal',Path(__file__).parents[1]/'marble-thermal-report.py')
thermal=importlib.util.module_from_spec(spec);spec.loader.exec_module(thermal)


def line(t,limit=55000,temp=42000,valid=1):
    return f'marble-thermal: valid={valid} sensors=19 maximum_mc={temp} controller=0 sensor=14 limit_mc={limit} ok=1 at_us={t}\n'


class ThermalReport(unittest.TestCase):
    def inspect(self,text):
        with tempfile.TemporaryDirectory() as tmp:
            p=Path(tmp)/'log';p.write_text(text);return thermal.inspect(p)

    def normal(self):
        return line(1000000,40000,35000)+''.join(line(t*1000000) for t in range(2,8))+'kernel returned: log at 0, 1 bytes\n'

    def test_complete_interval_samples(self):
        r=self.inspect(self.normal());self.assertTrue(r['passed']);self.assertEqual(r['maximum_mc'],42000)

    def test_conservative_cpu_cap_threshold(self):
        text=self.normal().replace('limit_mc=55000','limit_mc=45000')
        self.assertTrue(self.inspect(text)['passed'])
        self.assertFalse(self.inspect(text.replace('maximum_mc=42000','maximum_mc=45000'))['passed'])
        self.assertFalse(self.inspect(text.replace('limit_mc=45000','limit_mc=99000'))['passed'])

    def test_gap_rejected(self):
        r=self.inspect(self.normal().replace('at_us=4000000','at_us=40000000'))
        self.assertFalse(r['passed'])

    def test_hot_or_invalid_rejected(self):
        for a,b in [('maximum_mc=42000','maximum_mc=55000'),('valid=1','valid=0'),('sensors=19','sensors=18')]:
            self.assertFalse(self.inspect(self.normal().replace(a,b))['passed'])

    def test_panic_return_is_not_normal_completion(self):
        self.assertFalse(self.inspect(self.normal()+'panic(cpu 0): failed\n')['passed'])

    def test_latched_stop_rejected(self):
        self.assertFalse(self.inspect(self.normal()+'marble-thermal: STOP requested\n')['passed'])

    def test_missing_runtime_samples_rejected(self):
        self.assertFalse(self.inspect(line(1000000,40000,35000)+'kernel returned: log\n')['passed'])

    def test_short_duration_needs_paired_complete_window_evidence(self):
        def window(t,starting=0,span=0,reason=0):
            return f'marble-thermal-window: profile=short18-v1 starting={starting} hot50_ms={span} max_hot50_ms={span} reason={reason} ok=1 at_us={t}\n'
        text=line(1000000,50000,42000)+window(1000000,1)
        text+=''.join(line(t*1000000,55000,51000)+window(t*1000000,span=(t-2)*1000) for t in range(2,8))
        text+='kernel returned: log at 0, 1 bytes\n'
        r=self.inspect(text);self.assertTrue(r['passed']);self.assertEqual(r['max_observed_consecutive_ge50_ms'],5000)
        preview=text.replace('profile=short18-v1','profile=gpu90-v1')
        self.assertTrue(self.inspect(preview)['passed'])
        self.assertEqual(self.inspect(preview)['thermal_profile'],'gpu90-v1')
        sustained=preview.replace('profile=gpu90-v1','profile=gpu90-v2').replace('max_hot50_ms=5000','max_hot50_ms=20000')
        self.assertTrue(self.inspect(sustained)['passed'])
        self.assertFalse(self.inspect(sustained.replace('max_hot50_ms=20000','max_hot50_ms=60000'))['passed'])
        self.assertFalse(self.inspect(sustained.replace('maximum_mc=51000','maximum_mc=55000'))['passed'])
        self.assertFalse(self.inspect(preview.replace('profile=gpu90-v1','profile=unknown',1))['passed'])
        self.assertFalse(self.inspect(preview.replace('profile=gpu90-v1','profile=short18-v1',1))['passed'])
        for invalid in [text.replace('max_hot50_ms=5000','max_hot50_ms=10000'),
                        text.replace(window(4000000,span=2000),''),text.replace('reason=0','reason=5')]:
            self.assertFalse(self.inspect(invalid)['passed'])
        self.assertFalse(self.inspect(text.replace('marble-thermal-window:','unrecognized:'))['passed'])


if __name__=='__main__':unittest.main()
