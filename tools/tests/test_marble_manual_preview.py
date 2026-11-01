import importlib.util
import json
from pathlib import Path
import sys
import unittest
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools'))
spec=importlib.util.spec_from_file_location('manual_preview_run',ROOT/'tools/marble-run.py')
runner=importlib.util.module_from_spec(spec);spec.loader.exec_module(runner)


class ManualPreview(unittest.TestCase):
    def test_exact_explicit_duration_and_guards(self):
        path=ROOT/'out/gpu-scale-v4-layout-plan/mach.marble-source'
        if not path.exists():self.skipTest('candidate not built')
        data=path.read_bytes();profile=json.loads(path.with_suffix('.json').read_text())
        options=dict(source_profile=profile,cpu_cap=True,efi_idle=True,gpu_preview=True,
            cpus=4,input_mailbox=True,gpu_probe=17,gpu_runtime=True,gpu_composition=True,
            display_pacing='hardware',render_scale=2,manual_ten_minute=True)
        result=runner.payload(data,b'RAM',7,600,**options)
        for flag in (b'loader_return_after=600',b'marble_gpu_manual=1',b'marble_gpu_preview=1',
                     b'marble_cpu_cap=1',b'marble_efi_idle=1',b'ufs_write=0',b'fb_hw_vsync=1'):
            self.assertIn(flag,result)
        for duration in (90,91,599,601,3600):
            with self.assertRaises(ValueError):runner.payload(data,b'RAM',7,duration,**options)
        for change in (dict(manual_ten_minute=False),dict(cpu_cap=False),dict(efi_idle=False),
                       dict(gpu_preview=False),dict(gpu_runtime=False),dict(gpu_composition=False),
                       dict(cpus=1),dict(input_mailbox=False),dict(gpu_probe=0),dict(short_thermal=True),
                       dict(display_pacing='immediate')):
            with self.assertRaises(ValueError):runner.payload(data,b'RAM',7,600,**dict(options,**change))


if __name__=='__main__':unittest.main()
