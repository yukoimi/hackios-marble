import hashlib
import importlib.util
import json
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT/'tools'))
from marble_source import MARKER, XNU_BASE, verify_kernel

spec = importlib.util.spec_from_file_location('marble_source_run', ROOT/'tools/marble-run.py')
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)


class SourceKernelTests(unittest.TestCase):
    def test_stream24_is_distinct_from_legacy_refill_and_requires_exact_disk(self):
        base=ROOT/'out/audio-stream-window-v242-plan';kernel=(base/'mach.marble-source').read_bytes();disk=(base/'test.apfs').read_bytes()
        options=dict(source_profile=json.loads((base/'mach.json').read_text()),blank_display=True,audio_queue_probe=True,cpu_cap=True,efi_idle=True,short_thermal=True,
                     audio_owner_package=(ROOT/'out/adsp-owned-package-v1-plan/dependencies.bin').read_bytes(),audio_owner_periods=24,audio_owner_pcm=True,audio_board_held=True,audio_owner_stream=True)
        data=runner.payload(kernel,disk,7,18,**options)
        self.assertIn(b'marble_audio_stream=1',data);self.assertIn(b'marble_audio_owner_periods=24',data);self.assertNotIn(b'marble_audio_refill=1',data)
        for change in [dict(audio_owner_stream=False),dict(audio_owner_refill=True),dict(audio_owner_periods=8),dict(audio_board_held=False),dict(audio_owner_pcm=False)]:
            with self.assertRaises(ValueError):runner.payload(kernel,disk,7,18,**dict(options,**change))
        with self.assertRaises(ValueError):runner.payload(kernel,(ROOT/'out/audio-refill-v238-plan/test.apfs').read_bytes(),7,18,**options)
    def test_live_refill_requires_exact_producer_kernel_and_held_profile(self):
        base=ROOT/'out/audio-refill-v238-plan'
        kernel=(base/'mach.marble-source').read_bytes();disk=(base/'test.apfs').read_bytes()
        profile=json.loads((base/'mach.json').read_text());package=(ROOT/'out/adsp-owned-package-v1-plan/dependencies.bin').read_bytes()
        options=dict(source_profile=profile,blank_display=True,audio_queue_probe=True,cpu_cap=True,efi_idle=True,short_thermal=True,
                     audio_owner_package=package,audio_owner_periods=8,audio_owner_pcm=True,audio_board_held=True,audio_owner_refill=True)
        data=runner.payload(kernel,disk,7,18,**options)
        self.assertIn(b'marble_audio_refill=1',data)
        for update in ({'audio_board_held':False},{'audio_owner_pcm':False},{'audio_owner_refill':False},{'audio_owner_package':None}):
            with self.assertRaises(ValueError):runner.payload(kernel,disk,7,18,**dict(options,**update))
        with self.assertRaises(ValueError):runner.payload(kernel,(ROOT/'out/audio-board-held-v237-plan/test.apfs').read_bytes(),7,18,**options)
        old=ROOT/'out/audio-board-held-v237-plan'
        with self.assertRaises(ValueError):runner.payload((old/'mach.marble-source').read_bytes(),disk,7,18,**dict(options,source_profile=json.loads((old/'mach.json').read_text())))
    def test_signal_disk_is_restricted_to_explicit_PCM_owner(self):
        base=ROOT/'out/audio-owner-PCM-v222-plan'
        data=(base/'mach.marble-source').read_bytes();profile=json.loads((base/'mach.json').read_text())
        disk=(ROOT/'out/audio-signal-v223b-plan/test.apfs').read_bytes();package=(ROOT/'out/adsp-owned-package-v1-plan/dependencies.bin').read_bytes()
        options=dict(source_profile=profile,blank_display=True,audio_queue_probe=True,cpu_cap=True,efi_idle=True,short_thermal=True,audio_owner_package=package,audio_owner_periods=8,audio_owner_pcm=True)
        self.assertIn(b'marble_audio_owner_pcm=1',runner.payload(data,disk,7,18,**options))
        with self.assertRaises(ValueError):runner.payload(data,disk,7,18,**dict(options,audio_owner_pcm=False))
        with self.assertRaises(ValueError):runner.payload(data,disk+b'changed',7,18,**options)
    def test_owner_PCM_requires_explicit_new_kernel_and_eight_periods(self):
        base=ROOT/'out/audio-owner-PCM-v222-plan'
        data=(base/'mach.marble-source').read_bytes();profile=json.loads((base/'mach.json').read_text())
        disk=(base/'test.apfs').read_bytes();package=(ROOT/'out/adsp-owned-package-v1-plan/dependencies.bin').read_bytes()
        options=dict(source_profile=profile,blank_display=True,audio_queue_probe=True,cpu_cap=True,
                     efi_idle=True,short_thermal=True,audio_owner_package=package,audio_owner_periods=8,audio_owner_pcm=True)
        self.assertIn(b'marble_audio_owner_pcm=1',runner.payload(data,disk,7,18,**options))
        for update in ({'audio_owner_periods':2},{'audio_owner_package':None},{'short_thermal':False}):
            with self.assertRaises(ValueError):runner.payload(data,disk,7,18,**dict(options,**update))
        old=ROOT/'out/audio-owner-eight-v219-plan'
        with self.assertRaises(ValueError):runner.payload((old/'mach.marble-source').read_bytes(),disk,7,18,**dict(options,source_profile=json.loads((old/'mach.json').read_text())))
    def test_owner_eight_requires_explicit_matching_kernel_and_package(self):
        base=ROOT/'out/audio-owner-eight-v219-plan'
        data=(base/'mach.marble-source').read_bytes();profile=json.loads((base/'mach.json').read_text())
        disk=(base/'test.apfs').read_bytes();package=(ROOT/'out/adsp-owned-package-v1-plan/dependencies.bin').read_bytes()
        options=dict(source_profile=profile,blank_display=True,audio_queue_probe=True,cpu_cap=True,
                     efi_idle=True,short_thermal=True,audio_owner_package=package,audio_owner_periods=8)
        payload=runner.payload(data,disk,7,18,**options)
        self.assertIn(b'marble_audio_owner_periods=8',payload)
        self.assertIn(b'marble_audio_owner=1',payload)
        for update in ({'audio_owner_periods':3},{'audio_owner_package':None},{'audio_queue_probe':False},{'short_thermal':False}):
            with self.assertRaises(ValueError):runner.payload(data,disk,7,18,**dict(options,**update))
        old=ROOT/'out/audio-owner-kernel-v213-plan'
        with self.assertRaises(ValueError):
            runner.payload((old/'mach.marble-source').read_bytes(),disk,7,18,
                           **dict(options,source_profile=json.loads((old/'mach.json').read_text())))
    def test_native_audio_transport_requires_explicit_short_queue_and_ABI(self):
        base=ROOT/'out/audio-native-transport-v206-plan'
        data=(base/'mach.marble-source').read_bytes();profile=json.loads((base/'mach.json').read_text())
        disk=(base/'test.apfs').read_bytes()
        options=dict(source_profile=profile,blank_display=True,audio_queue_probe=True,
                     cpu_cap=True,efi_idle=True,short_thermal=True,audio_transport_probe=True)
        self.assertIn(b'marble_audio_transport=1',runner.payload(data,disk,7,18,**options))
        for update in ({'audio_queue_probe':False},{'short_thermal':False},{'blank_display':False},{'gpu_probe':1}):
            with self.assertRaises(ValueError):runner.payload(data,disk,7,18,**dict(options,**update))
        old=ROOT/'out/audio-queue-v195-plan'
        old_profile=json.loads((old/'mach.json').read_text())
        with self.assertRaises(ValueError):runner.payload((old/'mach.marble-source').read_bytes(),disk,7,18,**dict(options,source_profile=old_profile))

    def test_RAM_audio_queue_is_explicit_small_black_bounded_profile(self):
        data=(ROOT/'out/audio-queue-v195-plan/mach.marble-source').read_bytes()
        profile=json.loads((ROOT/'out/audio-queue-v195-plan/mach.json').read_text())
        options=dict(source_profile=profile,blank_display=True,audio_queue_probe=True)
        good=runner.payload(data,b'RAM',7,20,**options)
        self.assertIn(b'marble_audio_queue=1',good)
        self.assertIn(b'adsp=0',good)
        self.assertNotIn(b'marble_audio_queue=1',runner.payload(data,b'RAM',7,20,source_profile=profile,blank_display=True))
        for update in ({'blank_display':False},{'input_mailbox':True},{'gpu_probe':1},{'gpu_runtime':True}):
            with self.assertRaises(ValueError):runner.payload(data,b'RAM',7,20,**dict(options,**update))
        with self.assertRaises(ValueError):runner.payload(data,b'RAM',7,31,**options)
        with self.assertRaises(ValueError):runner.payload(data,bytes(0x400001),7,20,**options)

    def test_short_thermal_queue_pins_disk_and_duration(self):
        base=ROOT/'out/audio-queue-v195-plan'
        data=(base/'mach.marble-source').read_bytes()
        profile=json.loads((base/'mach.json').read_text())
        disk=(base/'test.apfs').read_bytes()
        options=dict(source_profile=profile,blank_display=True,audio_queue_probe=True,
                     cpu_cap=True,efi_idle=True,short_thermal=True)
        self.assertIn(b'marble_audio_queue=1',runner.payload(data,disk,7,18,**options))
        with self.assertRaises(ValueError):runner.payload(data,disk+b'changed',7,18,**options)
        with self.assertRaises(ValueError):runner.payload(data,disk,7,19,**options)
        for update in ({'audio_queue_probe':False},{'cpu_cap':False},{'efi_idle':False}):
            with self.assertRaises(ValueError):runner.payload(data,disk,7,18,**dict(options,**update))

    def test_boot_refuses_failed_candidate_even_with_matching_hash(self):
        image=b'fixture image'
        manifest={'output_sha256':hashlib.sha256(image).hexdigest()}
        runner.verify_boot_image(image,manifest)
        with self.assertRaises(ValueError):
            runner.verify_boot_image(image,dict(manifest,hardware_status='FAILED: lost loader'))
        with self.assertRaises(ValueError):
            runner.verify_boot_image(image+b'changed',manifest)
        with self.assertRaises(ValueError):
            runner.verify_boot_image(image,manifest,dict(image_sha256=manifest['output_sha256'],hardware_status='FAILED: interrupted stop'))
        with self.assertRaises(ValueError):
            runner.verify_boot_image(image,manifest,dict(image_sha256='wrong',hardware_status='PASSED'))
        with self.assertRaises(ValueError):
            runner.verify_boot_image(image,manifest,dict(image_sha256=manifest['output_sha256'],boot_allowed=False,hardware_status='IN_PROGRESS_DO_NOT_REBOOT'))
        runner.verify_boot_image(image,manifest,dict(image_sha256=manifest['output_sha256'],boot_allowed=True,hardware_status='PASS_TWO_REAL_DSP_NATIVE_ZERO2_PROVIDER_LIFECYCLES'))

    def test_explicit_improved_loader_gpu_preview_is_bounded(self):
        path=ROOT/'out/linux-build/mach-marble-cpucap-v3'
        if not path.exists():self.skipTest('thermal candidate not downloaded')
        data=path.read_bytes();profile=json.loads(path.with_suffix('.json').read_text())
        options=dict(source_profile=profile,cpu_cap=True,efi_idle=True,gpu_preview=True,
            cpus=4,input_mailbox=True,gpu_probe=17,gpu_runtime=True,gpu_composition=True)
        self.assertIn(b'marble_gpu_preview=1',runner.payload(data,b'RAM',7,90,**options))
        for update in ({'cpu_cap':False},{'efi_idle':False},{'cpus':1},{'gpu_probe':0},
                       {'gpu_runtime':False},{'gpu_composition':False},{'blank_display':True},
                       {'input_mailbox':False},{'short_thermal':True}):
            with self.assertRaises(ValueError):runner.payload(data,b'RAM',7,90,**dict(options,**update))
        with self.assertRaises(ValueError):runner.payload(data,b'RAM',7,91,**options)
        with self.assertRaises(ValueError):runner.payload(data,b'RAM',7,29,**options)

    def test_thermal_stopped_candidate_is_idle_only(self):
        path=ROOT/'out/linux-build/mach-marble-cpucap-v3'
        if not path.exists():self.skipTest('thermal candidate not downloaded')
        data=path.read_bytes();profile=json.loads(path.with_suffix('.json').read_text())
        profile=dict(profile,idle_only_after_thermal_stop=True,requires_cpu_cap=True)
        runner.payload(data,b'RAM',7,18,source_profile=profile,cpu_cap=True,blank_display=True)
        for options in ({},{'blank_display':True,'gpu_probe':1}):
            with self.assertRaises(ValueError):
                runner.payload(data,b'RAM',7,18,source_profile=profile,cpu_cap=True,**options)
        with self.assertRaises(ValueError):
            runner.payload(data,bytes(0x400001),7,18,source_profile=profile,cpu_cap=True,blank_display=True)

    def test_efi_wait_does_not_bypass_thermal_stop(self):
        path=ROOT/'out/linux-build/mach-marble-cpucap-v3'
        if not path.exists():self.skipTest('thermal candidate not downloaded')
        data=path.read_bytes();profile=json.loads(path.with_suffix('.json').read_text())
        result=runner.payload(data,b'RAM',7,18,source_profile=profile,cpu_cap=True,blank_display=True,efi_idle=True)
        self.assertIn(b'marble_efi_idle=1',result)
        for options in ({}, {'blank_display':True,'gpu_probe':1}, {'gpu_runtime':True}):
            with self.assertRaises(ValueError):
                runner.payload(data,b'RAM',7,18,source_profile=profile,cpu_cap=True,efi_idle=True,**options)
        with self.assertRaises(ValueError):
            runner.payload(data,b'RAM',7,18,source_profile=profile,blank_display=True,efi_idle=True)
        with self.assertRaises(ValueError):
            runner.payload(data,b'RAM',7,91,source_profile=profile,cpu_cap=True,blank_display=True,efi_idle=True)

    def test_default_profile_stays_pinned_and_telemetry_is_opt_in(self):
        data = (ROOT/'prebuilt/mach.development.bcm2837').read_bytes()
        profile = {'profile':'marble-source-v1', 'xnu_base':XNU_BASE,
                   'kernel_sha256':hashlib.sha256(data).hexdigest()}
        with self.assertRaises(ValueError):
            verify_kernel(data, profile)
        with self.assertRaises(ValueError):
            runner.payload(data, b'RAM', 7, 30, diagnostics=True)
        with self.assertRaises(ValueError):
            runner.payload(data, b'RAM', 7, 30, fb_threads=2)
        with self.assertRaises(ValueError):
            runner.payload(data+b'changed', b'RAM', 7, 30)

    def test_short_thermal_pins_program_and_does_not_unlock_ios_gpu(self):
        path=ROOT/'out/linux-build/mach-marble-cpucap-v3'
        if not path.exists():self.skipTest('thermal candidate not downloaded')
        data=path.read_bytes();profile=json.loads(path.with_suffix('.json').read_text())
        disk=(ROOT/'out/marble-thermal-idle-minimal.apfs').read_bytes()
        options=dict(source_profile=profile,cpu_cap=True,efi_idle=True,short_thermal=True,blank_display=True)
        self.assertIn(b'marble_thermal_short=1',runner.payload(data,disk,7,18,**options))
        for update in ({'gpu_probe':1},{'gpu_runtime':True},{'blank_display':False},{'input_mailbox':True},{'cpu_cap':False}):
            with self.assertRaises(ValueError):runner.payload(data,disk,7,18,**dict(options,**update))
        with self.assertRaises(ValueError):runner.payload(data,disk+b'changed',7,18,**options)
        with self.assertRaises(ValueError):runner.payload(data,disk,7,19,**options)

    def test_built_candidate_identity_and_marker_are_required(self):
        path = ROOT/'out/linux-build/mach.marble-source'
        if not path.exists():
            self.skipTest('source build has not been downloaded')
        data = path.read_bytes()
        profile = json.loads(path.with_suffix('.json').read_text())
        verify_kernel(data, profile)
        for count in (1, 2, 4):
            runner.payload(data, b'RAM', 7, 30, cpus=count, source_profile=profile, diagnostics=True)
        with self.assertRaises(ValueError):
            verify_kernel(data+b'changed', profile)
        changed = data.replace(MARKER, b'X'*len(MARKER))
        modified = dict(profile, kernel_sha256=hashlib.sha256(changed).hexdigest())
        with self.assertRaises(ValueError):
            verify_kernel(changed, modified)


if __name__ == '__main__':
    unittest.main()
