"""Reject mixed firmware experiments before any directory or device operation."""
from pathlib import Path
import json,subprocess,sys,tempfile,unittest
ROOT=Path(__file__).resolve().parents[2]
class ProbeIsolation(unittest.TestCase):
 def test_refuses_combined_modes_before_phone(self):
  with tempfile.TemporaryDirectory() as tmp:
   out=Path(tmp)/'not-created'
   for mode in ('--adsp-tdm-pcm-graph-cycle','--adsp-tdm-run-cycle','--adsp-audio-clock-cycle','--adsp-tdm-config-cycle','--adsp-tdm-graph-cycle','--adsp-amdb-load-cycle','--adsp-amdb-info','--i2c-config-snapshot','--i2c-dt-info','--i2c-inventory','--adsp-spf-state','--adsp-apps-open','--adsp-control-boot','--adsp-scm-held-stage','--glink-smem-info','--adsp-scm-ipcc-stage','--qmp-ipcc-cycle','--adsp-scm-bounded-stage','--qmp-smccc-cycle','--qmp-tpl-cycle','--adsp-scm-channel-stage','--adsp-scm-wire-stage','--qmp-wire-cycle','--adsp-scm-qmp-stage','--adsp-scm-stage','--icb-run-scm-bandwidth-cycle','--adsp-qmp-stage','--qmp-mailbox-load-state-cycle','--rpmh-zero-cycle','--adsp-icb-info','--adsp-dependency-info','--icb-zero-cycle','--xo-npa-test','--xo-rpmh-hold','--icb-bandwidth-cycle','--icb-run-cycle','--icb-run-bandwidth-cycle','--adsp-dependency-cycle','--adsp-owned-stage','--adsp-owned-boot'):
    for extra in ([],['--discovery-only','--exercise'],['--discovery-only','--pil-driver','C:/missing.efi'],
                  ['--discovery-only','--qmp-native-settled-close'],['--discovery-only','--adsp-xo-info']):
     r=subprocess.run([sys.executable,'-X','utf8','tools/marble-gpu-preview.py','--out',str(out),mode,*extra],cwd=ROOT,capture_output=True,text=True,timeout=8)
     self.assertEqual(r.returncode,2,r.stdout+r.stderr);self.assertIn('requires isolated discovery',r.stderr);self.assertFalse(out.exists())
 def test_dedicated_glink_image_rejects_other_experiments(self):
  with tempfile.TemporaryDirectory() as tmp:
   base=Path(tmp);out=base/'not-created'
   (base/'build-profile.json').write_text(json.dumps(dict(glink_only=True)))
   for extra in ([],['--discovery-only'],['--discovery-only','--qmp-ipcc-cycle'],['--discovery-only','--icb-run-scm-bandwidth-cycle']):
    r=subprocess.run([sys.executable,'-X','utf8','tools/marble-gpu-preview.py','--out',str(out),'--boot-image',str(base/'loader-boot.img'),*extra],cwd=ROOT,capture_output=True,text=True,timeout=8)
    self.assertEqual(r.returncode,2,r.stdout+r.stderr);self.assertIn('dedicated GLink image requires isolated discovery',r.stderr);self.assertFalse(out.exists())
 def test_glink_build_requires_isolated_firmware_profile(self):
  with tempfile.TemporaryDirectory() as tmp:
   out=Path(tmp)/'not-created'
   for extra in ([],['--firmware-only','--adsp-only'],['--firmware-only','--qmp-only'],['--firmware-only','--icb-only']):
    r=subprocess.run([sys.executable,'-X','utf8','tools/marble-build-brightness.py','--out',str(out),'--glink-only',*extra],cwd=ROOT,capture_output=True,text=True,timeout=8)
    self.assertEqual(r.returncode,2,r.stdout+r.stderr);self.assertIn('--glink-only requires --firmware-only and cannot combine other profiles',r.stderr);self.assertFalse(out.exists())
 def test_dedicated_held_image_rejects_other_experiments(self):
  with tempfile.TemporaryDirectory() as tmp:
   base=Path(tmp);out=base/'not-created'
   (base/'build-profile.json').write_text(json.dumps(dict(held_only=True)))
   for extra in ([],['--discovery-only'],['--discovery-only','--adsp-scm-ipcc-stage'],['--discovery-only','--glink-smem-info']):
    r=subprocess.run([sys.executable,'-X','utf8','tools/marble-gpu-preview.py','--out',str(out),'--boot-image',str(base/'loader-boot.img'),*extra],cwd=ROOT,capture_output=True,text=True,timeout=8)
    self.assertEqual(r.returncode,2,r.stdout+r.stderr);self.assertIn('dedicated held image requires isolated discovery',r.stderr);self.assertFalse(out.exists())
 def test_held_build_requires_isolated_firmware_profile(self):
  with tempfile.TemporaryDirectory() as tmp:
   out=Path(tmp)/'not-created'
   for extra in ([],['--firmware-only','--adsp-only'],['--firmware-only','--qmp-only'],['--firmware-only','--glink-only']):
    r=subprocess.run([sys.executable,'-X','utf8','tools/marble-build-brightness.py','--out',str(out),'--held-only',*extra],cwd=ROOT,capture_output=True,text=True,timeout=8)
    self.assertEqual(r.returncode,2,r.stdout+r.stderr);self.assertIn('--held-only requires --firmware-only and cannot combine other profiles',r.stderr);self.assertFalse(out.exists())
 def test_dedicated_control_image_rejects_other_experiments(self):
  with tempfile.TemporaryDirectory() as tmp:
   base=Path(tmp);out=base/'not-created'
   (base/'build-profile.json').write_text(json.dumps(dict(control_only=True)))
   for extra in ([],['--discovery-only'],['--discovery-only','--adsp-scm-held-stage'],['--discovery-only','--glink-smem-info']):
    r=subprocess.run([sys.executable,'-X','utf8','tools/marble-gpu-preview.py','--out',str(out),'--boot-image',str(base/'loader-boot.img'),*extra],cwd=ROOT,capture_output=True,text=True,timeout=8)
    self.assertEqual(r.returncode,2,r.stdout+r.stderr);self.assertIn('dedicated control image requires isolated discovery',r.stderr);self.assertFalse(out.exists())
 def test_control_build_requires_isolated_firmware_profile(self):
  with tempfile.TemporaryDirectory() as tmp:
   out=Path(tmp)/'not-created'
   for extra in ([],['--firmware-only','--adsp-only'],['--firmware-only','--held-only'],['--firmware-only','--glink-only']):
    r=subprocess.run([sys.executable,'-X','utf8','tools/marble-build-brightness.py','--out',str(out),'--control-only',*extra],cwd=ROOT,capture_output=True,text=True,timeout=8)
    self.assertEqual(r.returncode,2,r.stdout+r.stderr);self.assertIn('--control-only requires --firmware-only and cannot combine other profiles',r.stderr);self.assertFalse(out.exists())
 def test_dedicated_apps_image_rejects_other_experiments(self):
  with tempfile.TemporaryDirectory() as tmp:
   base=Path(tmp);out=base/'not-created'
   (base/'build-profile.json').write_text(json.dumps(dict(apps_only=True)))
   for extra in ([],['--discovery-only'],['--discovery-only','--adsp-control-boot'],['--discovery-only','--glink-smem-info']):
    r=subprocess.run([sys.executable,'-X','utf8','tools/marble-gpu-preview.py','--out',str(out),'--boot-image',str(base/'loader-boot.img'),*extra],cwd=ROOT,capture_output=True,text=True,timeout=8)
    self.assertEqual(r.returncode,2,r.stdout+r.stderr);self.assertIn('dedicated apps image requires isolated discovery',r.stderr);self.assertFalse(out.exists())
 def test_apps_build_requires_isolated_firmware_profile(self):
  with tempfile.TemporaryDirectory() as tmp:
   out=Path(tmp)/'not-created'
   for extra in ([],['--firmware-only','--adsp-only'],['--firmware-only','--control-only'],['--firmware-only','--held-only']):
    r=subprocess.run([sys.executable,'-X','utf8','tools/marble-build-brightness.py','--out',str(out),'--apps-only',*extra],cwd=ROOT,capture_output=True,text=True,timeout=8)
    self.assertEqual(r.returncode,2,r.stdout+r.stderr);self.assertIn('--apps-only requires --firmware-only and cannot combine other profiles',r.stderr);self.assertFalse(out.exists())
 def test_gpr_image_rejects_other_experiments(self):
  with tempfile.TemporaryDirectory() as tmp:
   base=Path(tmp);out=base/'not-created'
   (base/'build-profile.json').write_text(json.dumps(dict(gpr_only=True)))
   for extra in ([],['--discovery-only'],['--discovery-only','--adsp-apps-open'],['--discovery-only','--adsp-control-boot']):
    r=subprocess.run([sys.executable,'-X','utf8','tools/marble-gpu-preview.py','--out',str(out),'--boot-image',str(base/'loader-boot.img'),*extra],cwd=ROOT,capture_output=True,text=True,timeout=8)
    self.assertEqual(r.returncode,2,r.stdout+r.stderr);self.assertIn('dedicated GPR image requires isolated',r.stderr);self.assertFalse(out.exists())
 def test_gpr_build_requires_isolated_firmware(self):
  with tempfile.TemporaryDirectory() as tmp:
   out=Path(tmp)/'not-created'
   for extra in ([],['--firmware-only','--apps-only'],['--firmware-only','--control-only'],['--firmware-only','--glink-only']):
    r=subprocess.run([sys.executable,'-X','utf8','tools/marble-build-brightness.py','--out',str(out),'--gpr-only',*extra],cwd=ROOT,capture_output=True,text=True,timeout=8)
    self.assertEqual(r.returncode,2,r.stdout+r.stderr);self.assertIn('--gpr-only requires isolated --firmware-only',r.stderr);self.assertFalse(out.exists())
 def test_i2c_image_rejects_other_experiments(self):
  with tempfile.TemporaryDirectory() as tmp:
   base=Path(tmp);out=base/'not-created'
   (base/'build-profile.json').write_text(json.dumps(dict(i2c_only=True)))
   for extra in ([],['--discovery-only'],['--discovery-only','--adsp-spf-state'],['--discovery-only','--glink-smem-info']):
    r=subprocess.run([sys.executable,'-X','utf8','tools/marble-gpu-preview.py','--out',str(out),'--boot-image',str(base/'loader-boot.img'),*extra],cwd=ROOT,capture_output=True,text=True,timeout=8)
    self.assertEqual(r.returncode,2,r.stdout+r.stderr);self.assertIn('dedicated I2C image requires isolated',r.stderr);self.assertFalse(out.exists())
 def test_i2c_build_requires_isolated_firmware(self):
  with tempfile.TemporaryDirectory() as tmp:
   out=Path(tmp)/'not-created'
   for extra in ([],['--firmware-only','--apps-only'],['--firmware-only','--gpr-only'],['--firmware-only','--qmp-only']):
    r=subprocess.run([sys.executable,'-X','utf8','tools/marble-build-brightness.py','--out',str(out),'--i2c-only',*extra],cwd=ROOT,capture_output=True,text=True,timeout=8)
    self.assertEqual(r.returncode,2,r.stdout+r.stderr);self.assertIn('--i2c-only requires isolated --firmware-only',r.stderr);self.assertFalse(out.exists())
 def test_amdb_profile_is_exclusive(self):
  with tempfile.TemporaryDirectory() as tmp:
   base=Path(tmp);out=base/'not-created'
   (base/'build-profile.json').write_text(json.dumps(dict(amdb_only=True,gpr_only=True)))
   for extra in ([],['--discovery-only'],['--discovery-only','--adsp-spf-state']):
    r=subprocess.run([sys.executable,'-X','utf8','tools/marble-gpu-preview.py','--out',str(out),'--boot-image',str(base/'loader-boot.img'),*extra],cwd=ROOT,capture_output=True,text=True,timeout=8)
    self.assertEqual(r.returncode,2,r.stdout+r.stderr);self.assertIn('dedicated AMDB image',r.stderr);self.assertFalse(out.exists())
   for extra in ([],['--firmware-only','--gpr-only'],['--firmware-only','--i2c-only'],['--firmware-only','--apps-only']):
    r=subprocess.run([sys.executable,'-X','utf8','tools/marble-build-brightness.py','--out',str(out),'--amdb-only',*extra],cwd=ROOT,capture_output=True,text=True,timeout=8)
    self.assertEqual(r.returncode,2,r.stdout+r.stderr);self.assertIn('--amdb-only requires isolated',r.stderr);self.assertFalse(out.exists())
 def test_amdb_library_profile_is_exclusive(self):
  with tempfile.TemporaryDirectory() as tmp:
   base=Path(tmp);out=base/'not-created'
   (base/'build-profile.json').write_text(json.dumps(dict(amdb_load_only=True,gpr_only=True)))
   for extra in ([],['--discovery-only'],['--discovery-only','--adsp-spf-state'],['--discovery-only','--adsp-amdb-info']):
    r=subprocess.run([sys.executable,'-X','utf8','tools/marble-gpu-preview.py','--out',str(out),'--boot-image',str(base/'loader-boot.img'),*extra],cwd=ROOT,capture_output=True,text=True,timeout=8)
    self.assertEqual(r.returncode,2,r.stdout+r.stderr);self.assertIn('dedicated AMDB library image',r.stderr);self.assertFalse(out.exists())
   for extra in ([],['--firmware-only','--gpr-only'],['--firmware-only','--i2c-only'],['--firmware-only','--amdb-only']):
    r=subprocess.run([sys.executable,'-X','utf8','tools/marble-build-brightness.py','--out',str(out),'--amdb-load-only',*extra],cwd=ROOT,capture_output=True,text=True,timeout=8)
    self.assertEqual(r.returncode,2,r.stdout+r.stderr);self.assertIn('--amdb-load-only requires isolated',r.stderr);self.assertFalse(out.exists())
 def test_tdm_graph_profile_is_exclusive(self):
  with tempfile.TemporaryDirectory() as tmp:
   base=Path(tmp);out=base/'not-created'
   (base/'build-profile.json').write_text(json.dumps(dict(tdm_graph_only=True,gpr_only=True)))
   for extra in ([],['--discovery-only'],['--discovery-only','--adsp-amdb-load-cycle'],['--discovery-only','--adsp-amdb-info']):
    r=subprocess.run([sys.executable,'-X','utf8','tools/marble-gpu-preview.py','--out',str(out),'--boot-image',str(base/'loader-boot.img'),*extra],cwd=ROOT,capture_output=True,text=True,timeout=8)
    self.assertEqual(r.returncode,2,r.stdout+r.stderr);self.assertIn('dedicated TDM graph image',r.stderr);self.assertFalse(out.exists())
   for extra in ([],['--firmware-only','--gpr-only'],['--firmware-only','--i2c-only'],['--firmware-only','--amdb-only'],['--firmware-only','--amdb-load-only']):
    r=subprocess.run([sys.executable,'-X','utf8','tools/marble-build-brightness.py','--out',str(out),'--tdm-graph-only',*extra],cwd=ROOT,capture_output=True,text=True,timeout=8)
    self.assertEqual(r.returncode,2,r.stdout+r.stderr);self.assertIn('--tdm-graph-only requires isolated',r.stderr);self.assertFalse(out.exists())
 def test_tdm_config_profile_is_exclusive(self):
  with tempfile.TemporaryDirectory() as tmp:
   base=Path(tmp);out=base/'not-created'
   (base/'build-profile.json').write_text(json.dumps(dict(tdm_config_only=True,tdm_graph_only=True,gpr_only=True)))
   for extra in ([],['--discovery-only'],['--discovery-only','--adsp-tdm-graph-cycle']):
    r=subprocess.run([sys.executable,'-X','utf8','tools/marble-gpu-preview.py','--out',str(out),'--boot-image',str(base/'loader-boot.img'),*extra],cwd=ROOT,capture_output=True,text=True,timeout=8)
    self.assertEqual(r.returncode,2,r.stdout+r.stderr);self.assertIn('dedicated TDM config image',r.stderr);self.assertFalse(out.exists())
   for extra in ([],['--firmware-only','--tdm-graph-only'],['--firmware-only','--i2c-only'],['--firmware-only','--amdb-load-only']):
    r=subprocess.run([sys.executable,'-X','utf8','tools/marble-build-brightness.py','--out',str(out),'--tdm-config-only',*extra],cwd=ROOT,capture_output=True,text=True,timeout=8)
    self.assertEqual(r.returncode,2,r.stdout+r.stderr);self.assertIn('--tdm-config-only requires isolated',r.stderr);self.assertFalse(out.exists())
 def test_audio_clock_profile_is_exclusive(self):
  with tempfile.TemporaryDirectory() as tmp:
   base=Path(tmp);out=base/'not-created'
   (base/'build-profile.json').write_text(json.dumps(dict(audio_clock_only=True,gpr_only=True)))
   for extra in ([],['--discovery-only'],['--discovery-only','--adsp-tdm-graph-cycle']):
    r=subprocess.run([sys.executable,'-X','utf8','tools/marble-gpu-preview.py','--out',str(out),'--boot-image',str(base/'loader-boot.img'),*extra],cwd=ROOT,capture_output=True,text=True,timeout=8)
    self.assertEqual(r.returncode,2,r.stdout+r.stderr);self.assertIn('dedicated audio clock image',r.stderr);self.assertFalse(out.exists())
   for extra in ([],['--firmware-only','--tdm-graph-only'],['--firmware-only','--i2c-only'],['--firmware-only','--amdb-load-only']):
    r=subprocess.run([sys.executable,'-X','utf8','tools/marble-build-brightness.py','--out',str(out),'--audio-clock-only',*extra],cwd=ROOT,capture_output=True,text=True,timeout=8)
    self.assertEqual(r.returncode,2,r.stdout+r.stderr);self.assertIn('--audio-clock-only requires isolated',r.stderr);self.assertFalse(out.exists())
 def test_tdm_run_profile_is_exclusive(self):
  with tempfile.TemporaryDirectory() as tmp:
   base=Path(tmp);out=base/'not-created'
   (base/'build-profile.json').write_text(json.dumps(dict(tdm_run_only=True,gpr_only=True)))
   for extra in ([],['--discovery-only'],['--discovery-only','--adsp-tdm-graph-cycle']):
    r=subprocess.run([sys.executable,'-X','utf8','tools/marble-gpu-preview.py','--out',str(out),'--boot-image',str(base/'loader-boot.img'),*extra],cwd=ROOT,capture_output=True,text=True,timeout=8)
    self.assertEqual(r.returncode,2,r.stdout+r.stderr);self.assertIn('dedicated TDM run image',r.stderr);self.assertFalse(out.exists())
   for extra in ([],['--firmware-only','--tdm-graph-only'],['--firmware-only','--i2c-only'],['--firmware-only','--amdb-load-only']):
    r=subprocess.run([sys.executable,'-X','utf8','tools/marble-build-brightness.py','--out',str(out),'--tdm-run-only',*extra],cwd=ROOT,capture_output=True,text=True,timeout=8)
    self.assertEqual(r.returncode,2,r.stdout+r.stderr);self.assertIn('--tdm-run-only requires isolated',r.stderr);self.assertFalse(out.exists())
 def test_pcm_graph_profile_is_exclusive(self):
  with tempfile.TemporaryDirectory() as tmp:
   base=Path(tmp);out=base/'not-created'
   (base/'build-profile.json').write_text(json.dumps(dict(tdm_pcm_graph_only=True,tdm_run_only=True,gpr_only=True)))
   for extra in ([],['--discovery-only'],['--discovery-only','--adsp-tdm-graph-cycle']):
    r=subprocess.run([sys.executable,'-X','utf8','tools/marble-gpu-preview.py','--out',str(out),'--boot-image',str(base/'loader-boot.img'),*extra],cwd=ROOT,capture_output=True,text=True,timeout=8)
    self.assertEqual(r.returncode,2,r.stdout+r.stderr);self.assertIn('dedicated PCM graph image',r.stderr);self.assertFalse(out.exists())
   for extra in ([],['--firmware-only','--tdm-graph-only'],['--firmware-only','--i2c-only'],['--firmware-only','--amdb-load-only']):
    r=subprocess.run([sys.executable,'-X','utf8','tools/marble-build-brightness.py','--out',str(out),'--tdm-pcm-graph-only',*extra],cwd=ROOT,capture_output=True,text=True,timeout=8)
    self.assertEqual(r.returncode,2,r.stdout+r.stderr);self.assertIn('--tdm-pcm-graph-only requires isolated',r.stderr);self.assertFalse(out.exists())
 def test_dma_inventory_requires_pcm_profile_and_explicit_modifier(self):
  with tempfile.TemporaryDirectory() as tmp:
   base=Path(tmp);out=base/'not-created'
   r=subprocess.run([sys.executable,'-X','utf8','tools/marble-gpu-preview.py','--out',str(out),'--discovery-only','--audio-dma-info'],cwd=ROOT,capture_output=True,text=True,timeout=8)
   self.assertEqual(r.returncode,2);self.assertIn('requires isolated',r.stderr);self.assertFalse(out.exists())
   r=subprocess.run([sys.executable,'-X','utf8','tools/marble-build-brightness.py','--out',str(out),'--firmware-only','--tdm-run-only','--audio-dma-inventory'],cwd=ROOT,capture_output=True,text=True,timeout=8)
   self.assertEqual(r.returncode,2);self.assertIn('requires --tdm-pcm-graph-only',r.stderr);self.assertFalse(out.exists())
   (base/'build-profile.json').write_text(json.dumps(dict(audio_dma_inventory=True,tdm_pcm_graph_only=True,tdm_run_only=True,gpr_only=True)))
   r=subprocess.run([sys.executable,'-X','utf8','tools/marble-gpu-preview.py','--out',str(out),'--boot-image',str(base/'loader-boot.img'),'--discovery-only','--adsp-tdm-pcm-graph-cycle'],cwd=ROOT,capture_output=True,text=True,timeout=8)
   self.assertEqual(r.returncode,2);self.assertIn('dedicated audio DMA image',r.stderr);self.assertFalse(out.exists())
if __name__=='__main__':unittest.main()
