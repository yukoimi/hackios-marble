import importlib.util
import struct
import tempfile
import unittest
from pathlib import Path


spec = importlib.util.spec_from_file_location('cadence', Path(__file__).resolve().parents[1] / 'marble-frame-cadence.py')
cadence = importlib.util.module_from_spec(spec)
spec.loader.exec_module(cadence)


class FrameCadenceTest(unittest.TestCase):
    def test_ring_order_and_measurement(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'recording.bin'
            header = struct.pack('<8sIIIIQ', b'RWREC003', 1, 1, 3, 1, 0)
            frames = b''.join(struct.pack('<Q', t) + b'\0\0\0' for t in (50_000_000, 0, 16_666_667))
            path.write_bytes(header + frames)
            times = cadence.timestamps(path)
            self.assertEqual(times, [0.0, 0.016666667, 0.05])
            result = cadence.measure(times, 0, 0.05, 60)
            self.assertAlmostEqual(result['observed_fps'], 40.0)
            self.assertEqual(result['intervals_over_1_5_refresh_periods'], 1)
            path.write_bytes(header + frames[:-1])
            with self.assertRaises(ValueError):
                cadence.timestamps(path)

    def test_interval_requires_two_frames(self):
        with self.assertRaises(ValueError):
            cadence.measure([1.0, 2.0], 1.0, 1.5, 60)

    def test_completed_composition_scope(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'recording.bin'
            path.write_bytes(struct.pack('<8sIIIIQ', b'RWREC004', 1, 1, 2, 0, 1_000_000_000) +
                             struct.pack('<Q3sQ3s', 0, b'abc', 16_666_667, b'def'))
            self.assertEqual(cadence.timestamps(path), [1.0, 1.016666667])
            scope = cadence.recording_scope(path)
            self.assertIn('multi-layer composition', scope)
            self.assertNotIn('bottom-layer', scope)


if __name__ == '__main__':
    unittest.main()
