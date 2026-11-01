import importlib.util
import json
from pathlib import Path
import sys
import unittest

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools'))
spec=importlib.util.spec_from_file_location('render_scale_run',ROOT/'tools/marble-run.py')
runner=importlib.util.module_from_spec(spec);spec.loader.exec_module(runner)


class RenderScale(unittest.TestCase):
    def test_opt_in_and_existing_guards(self):
        kernel=ROOT/'out/gpu-scale-v3-plan/mach.marble-source'
        if not kernel.exists():self.skipTest('source candidate not built')
        data=kernel.read_bytes();profile=json.loads(kernel.with_suffix('.json').read_text())
        options=dict(source_profile=profile,cpus=4,cpu_cap=True,efi_idle=True,input_mailbox=True,
            gpu_probe=17,gpu_runtime=True,gpu_composition=True,gpu_preview=True,display_pacing='hardware')
        normal=runner.payload(data,b'RAM',7,90,**options)
        self.assertNotIn(b'fbscale=',normal)
        scaled=runner.payload(data,b'RAM',7,90,render_scale=2,**options)
        self.assertIn(b'fbscale=2',scaled)
        for guard in (b'loader_return_after=90',b'ufs=0 ufs_write=0 ufs_native=0',b'marble_cpu_cap=1',b'marble_efi_idle=1',b'fb_hw_vsync=1'):
            self.assertIn(guard,scaled)
        for scale in (0,1,4,100):
            with self.assertRaises(ValueError):runner.payload(data,b'RAM',7,90,render_scale=scale,**options)
        with self.assertRaises(ValueError):runner.payload(data,b'RAM',7,90,render_scale=2,**dict(options,gpu_composition=False))
        with self.assertRaises(ValueError):runner.payload(data,b'RAM',7,90,render_scale=2,**dict(options,gpu_preview=False))
        with self.assertRaises(ValueError):runner.payload(data,b'RAM',7,91,render_scale=2,**options)


if __name__=='__main__':unittest.main()
