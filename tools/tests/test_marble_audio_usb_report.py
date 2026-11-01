import importlib.util
from pathlib import Path
import unittest

root = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('audio_usb', root/'tools/marble-audio-usb-report.py')
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)


class AudioUSBPolicy(unittest.TestCase):
    line = ('aud-usb-policy: version=1 post_auth_RAM_only=1 INFO_suppressed=30 '
            'capture_enabled=0 thermal_unchanged=1 PAS_required=1 audio_acceptance=0\n')

    def test_policy_is_only_sub_evidence(self):
        r = m.inspect(self.line)
        self.assertTrue(r['passed'], r)
        self.assertFalse(r['audio_acceptance'])

    def test_missing_duplicate_and_changed_protections_rejected(self):
        cases = ['', self.line * 2, self.line + 'aud-usb-policy: truncated\n',
                 self.line.replace('INFO_suppressed=30', 'INFO_suppressed=0'),
                 self.line.replace('INFO_suppressed=30', 'INFO_suppressed=100001'),
                 self.line.replace('PAS_required=1', 'PAS_required=1 PAS_required=1')]
        for key, value in m.inspect(self.line)['record'].items():
            if key != 'INFO_suppressed':
                cases.append(self.line.replace(f'{key}={value}', f'{key}={value ^ 1}'))
                cases.append(self.line.replace(f'{key}={value}', ''))
        for text in cases:
            with self.subTest(text=text):
                self.assertFalse(m.inspect(text)['passed'])


if __name__ == '__main__':
    unittest.main()
