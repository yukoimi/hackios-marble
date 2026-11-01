import importlib.util
from pathlib import Path
import struct
import sys
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT/'tools'))
from marble_smp import patched_kernel, SCAN_VA
spec = importlib.util.spec_from_file_location('smp_run', ROOT/'tools/marble-run.py')
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)


class SmpTests(unittest.TestCase):
    def test_patch_is_exact_and_rejects_changed_source(self):
        original = (ROOT/'prebuilt/mach.development.bcm2837').read_bytes()
        patched, offset = patched_kernel(original)
        self.assertEqual(original[:offset], patched[:offset])
        self.assertEqual(original[offset+4:], patched[offset+4:])
        self.assertEqual(struct.unpack_from('<I',patched,offset)[0],0x9141016c)
        with self.assertRaises(ValueError): patched_kernel(patched)
        altered = bytearray(original); altered[-1] ^= 1
        with self.assertRaises(ValueError): patched_kernel(altered)

    def test_cpu_profiles_require_matching_kernel(self):
        original = (ROOT/'prebuilt/mach.development.bcm2837').read_bytes()
        patched, _ = patched_kernel(original)
        for cpus in (2,4):
            data = runner.payload(patched,b'APFS',7,30,cpus=cpus)
            self.assertIn(f'cpus={cpus} '.encode(),data)
            self.assertIn(b'ufs_write=0 ufs_native=0 var_store=0',data)
            with self.assertRaises(ValueError): runner.payload(original,b'',7,30,cpus=cpus)
        with self.assertRaises(ValueError): runner.payload(patched,b'',7,30)
        with self.assertRaises(ValueError): runner.payload(original,b'',0,30,cpus=2)


if __name__ == '__main__': unittest.main()
