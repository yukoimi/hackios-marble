"""Reject malformed firmware before a future hardware loader can use its plan."""
import importlib.util
from pathlib import Path
import struct
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('mdt_plan', Path(__file__).parents[1]/'marble-mdt-plan.py')
mdt = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mdt)


class MdtPlan(unittest.TestCase):
    def fixture(self, directory, *, pa=0x88000000, filesz=4, memsz=4096, second=False):
        n = 4 if second else 3
        header_size = 52 + n * 32
        e = struct.pack('<16sHHIIIIIHHHHHH', b'\x7fELF\x01\x01\x01'+bytes(9),
                        2, 164, 1, pa, 52, 0, 0, 52, 32, n, 0, 0, 0)
        ph = [(0, 0, 0, 0, header_size, 0, 7 << 24, 0),
              (1, 4096, pa, pa, filesz, memsz, mdt.RELOCATABLE | 5, 4096)]
        if second:
            ph.append((1, 8192, pa, pa, 4, 4096, mdt.RELOCATABLE | 5, 4096))
        ph.append((0, 16384, 0, 0, 8, 8, mdt.TYPE_HASH, 4096))
        p = Path(directory)/'test.mdt'
        p.write_bytes(e + b''.join(struct.pack('<8I', *v) for v in ph) + b'hashdata')
        p.with_suffix('.b01').write_bytes(b'data')
        if second:
            p.with_suffix('.b02').write_bytes(b'data')
        p.with_suffix(f'.b{n-1:02d}').write_bytes(b'hashdata')
        return p

    def test_relocation_and_bss(self):
        with tempfile.TemporaryDirectory() as d:
            v = mdt.plan(self.fixture(d), 0x9fd00000, 4096)
            self.assertEqual(v['load_segments'][0]['physical'], 0x9fd00000)
            self.assertEqual(v['load_segments'][0]['zero_bytes'], 4092)
            self.assertFalse(v['authenticated'])

    def test_carveout_overflow(self):
        with tempfile.TemporaryDirectory() as d:
            with self.assertRaisesRegex(ValueError, 'carveout'):
                mdt.plan(self.fixture(d, memsz=8192), 0x9fd00000, 4096)

    def test_file_larger_than_memory(self):
        with tempfile.TemporaryDirectory() as d:
            with self.assertRaisesRegex(ValueError, 'extent'):
                mdt.plan(self.fixture(d, filesz=5000), 0x9fd00000, 4096)

    def test_missing_or_truncated_split(self):
        with tempfile.TemporaryDirectory() as d:
            p = self.fixture(d)
            p.with_suffix('.b01').write_bytes(b'bad')
            with self.assertRaisesRegex(ValueError, 'length'):
                mdt.plan(p, 0x9fd00000, 4096)

    def test_overlap(self):
        with tempfile.TemporaryDirectory() as d:
            with self.assertRaisesRegex(ValueError, 'overlapping'):
                mdt.plan(self.fixture(d, second=True), 0x9fd00000, 8192)

    def test_metadata_mismatch(self):
        with tempfile.TemporaryDirectory() as d:
            p = self.fixture(d)
            p.with_suffix('.b02').write_bytes(b'bad_hash')
            with self.assertRaisesRegex(ValueError, 'metadata'):
                mdt.plan(p, 0x9fd00000, 4096)

    def test_physical_wrap(self):
        with tempfile.TemporaryDirectory() as d:
            with self.assertRaisesRegex(ValueError, 'extent'):
                mdt.plan(self.fixture(d, pa=0xfffff000, memsz=8192), 0x9fd00000, 8192)


if __name__ == '__main__':
    unittest.main()
