"""Failure-oriented audit tests using selected real v208 hardware log lines."""
import importlib.util,unittest
from pathlib import Path
HERE=Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location('native_audio_report',HERE.parent/'marble-audio-native-report.py')
audit=importlib.util.module_from_spec(spec);spec.loader.exec_module(audit)
LOG=(HERE/'fixtures/marble-audio-native-v208.log').read_text()
PROOF_LOG=(HERE/'fixtures/marble-audio-native-v209.log').read_text()

class NativeAudit(unittest.TestCase):
 def test_real_native_zero_stream(self):
  r=audit.inspect(LOG);self.assertTrue(r['passed'],r['errors']);self.assertTrue(r['reset_safe'])
  self.assertFalse(r['audio_acceptance']);self.assertFalse(r['XNU_sink']);self.assertFalse(r['continuous_playback'])
 def test_missing_or_duplicate_boundary(self):
  for line in LOG.splitlines(keepends=True):
   if line.startswith(('aud-native: phase=stopped ','marble-smccc: PAS_stop ','marble-pil-held: restored ','marble-audio-map: result ','marble-aw-digital: complete ','marble-adsp-owned: complete ')):
    self.assertFalse(audit.inspect(LOG.replace(line,''))['passed'])
    self.assertFalse(audit.inspect(LOG+line)['passed'])
 def test_pas_must_precede_dma_free(self):
  lines=LOG.splitlines(keepends=True);p=next(i for i,l in enumerate(lines) if l.startswith('marble-smccc: PAS_stop '));m=next(i for i,l in enumerate(lines) if l.startswith('marble-audio-map: result '))
  lines[p],lines[m]=lines[m],lines[p];self.assertFalse(audit.inspect(''.join(lines))['reset_safe'])
 def test_time_fault_and_owner(self):
  for old,new in [('elapsed_us=609506','elapsed_us=1409506'),('completed=2','completed=1'),('HMUTE_retained=1','HMUTE_retained=0'),('retained=0','retained=1'),('PCM_requests=2','PCM_requests=0'),('owner=EFI','owner=unknown')]:
   self.assertIn(old,LOG);self.assertFalse(audit.inspect(LOG.replace(old,new))['passed'])
  for suffix in ['aud-native: phase=failure-retain\n','tr: q s=0 \n','pr: retired index=0\n']:
   self.assertFalse(audit.inspect(LOG+suffix)['passed'])
 def test_real_owner_proofs(self):
  r=audit.inspect(PROOF_LOG,require_proof=True);self.assertTrue(r['passed'],r['errors'])
  self.assertFalse(audit.inspect(LOG,require_proof=True)['passed'])
 def test_owner_proof_epoch_and_retirement_required(self):
  line=next(l for l in PROOF_LOG.splitlines(keepends=True) if l.startswith('aud-owner-proof: phase=retired '))
  for changed in ('',line+line,line.replace('flags=63','flags=31'),line.replace('epoch=13eb44b5','epoch=13eb44b6'),line.replace('UNMAP_typed=1','UNMAP_typed=0')):
   self.assertNotEqual(line,changed)
   self.assertFalse(audit.inspect(PROOF_LOG.replace(line,changed),require_proof=True)['passed'])

if __name__=='__main__':unittest.main()
