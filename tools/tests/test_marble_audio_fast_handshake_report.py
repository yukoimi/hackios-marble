"""Real v246 closure plus explicitly composed fast-service parser fixture."""
import unittest
from pathlib import Path
from test_marble_audio_stream_window_report import WindowReport,audit
LOG=(Path(__file__).parent/'fixtures/marble-audio-stream-v246.log').read_bytes().split(b'\0PMOC')[0].decode(errors='replace')
HEADER='aud-owner-handshake: service_us=10000 warmup_us=0 initial_ms=300 later_service_us=2000 minimum_runtime_us=300000 maximum_runtime_us=600000 real_replies_required=1 FIFO_owner=XNU IRQ_changes=0\n'
class FastReport(unittest.TestCase):
 def test_real_v246_closure(self):
  r=audit.inspect(LOG,**dict(WindowReport.args,runtime_sampling=True,usb_qmp_lease=True))
  self.assertTrue(r['passed'],r['errors']);self.assertFalse(r['continuous_playback'])
 def test_explicit_fast_service_header(self):
  args=dict(WindowReport.args,runtime_sampling=True,usb_qmp_lease=True,fast_handshake=True)
  r=audit.inspect(HEADER+LOG,**args);self.assertTrue(r['passed'],r['errors'])
  self.assertFalse(audit.inspect(LOG,**args)['passed'])
  for old,new in [('service_us=10000','service_us=100000'),('real_replies_required=1','real_replies_required=0'),('IRQ_changes=0','IRQ_changes=1'),('maximum_runtime_us=600000','maximum_runtime_us=750000')]:
   self.assertFalse(audit.inspect((HEADER+LOG).replace(old,new),**args)['passed'])
  self.assertFalse(audit.inspect(HEADER+LOG,**dict(args,fast_handshake=False))['passed'])
 def test_fifty_ms_requires_matching_evidence(self):
  args=dict(WindowReport.args,runtime_sampling=True,usb_qmp_lease=True,fast_handshake=True,handshake_ms=50)
  r=audit.inspect(HEADER.replace('service_us=10000','service_us=50000')+LOG,**args)
  self.assertTrue(r['passed'],r['errors'])
  self.assertFalse(audit.inspect(HEADER+LOG,**args)['passed'])
 def test_preserved_initial_hundred_ms(self):
  args=dict(WindowReport.args,runtime_sampling=True,usb_qmp_lease=True,fast_handshake=True,handshake_ms=50,warmup_ms=100)
  header=HEADER.replace('service_us=10000','service_us=50000').replace('warmup_us=0','warmup_us=100000')
  r=audit.inspect(header+LOG,**args);self.assertTrue(r['passed'],r['errors'])
  self.assertFalse(audit.inspect(HEADER+LOG,**args)['passed'])
 def test_prelaunch_single_CPU_and_original_cadence(self):
  args=dict(WindowReport.args,runtime_sampling=True,usb_qmp_lease=True,fast_handshake=True,handshake_ms=100,warmup_ms=100,prelaunch=True)
  header=HEADER.replace('service_us=10000','service_us=100000').replace('warmup_us=0','warmup_us=100000')
  early='aud-owner-prelaunch: PASS=1 phase=before-QMP-channel before_QMP_ON=1 cap_inactive=1 cap_result=0 CPU_ON_calls=1 deferred_cap_service=1 cap_request=1 elapsed_us=100000 limit_us=500000 DSP_start=0 FIFO_writes=0\n'
  reuse='aud-owner-prelaunch-reuse: PASS=1 CPU_ON_calls=1 no_second_CPU_ON=1 after_QMP_ON=1\n'
  text=(header+LOG).replace('marble-qmp-wire: opened ',early+'marble-qmp-wire: opened ').replace('aud-owner-fw: phase=kernel-ready-before-AUTH ',reuse+'aud-owner-fw: phase=kernel-ready-before-AUTH ')
  r=audit.inspect(text,**args);self.assertTrue(r['passed'],r['errors'])
  for old,new in [('CPU_ON_calls=1','CPU_ON_calls=2'),('cap_result=0','cap_result=2'),('deferred_cap_service=1','deferred_cap_service=0')]:
   self.assertFalse(audit.inspect(text.replace(old,new),**args)['passed'],old)
  self.assertFalse(audit.inspect(text,**dict(args,prelaunch=False))['passed'])
 def test_no_sampler_still_requires_actual_wall_clock(self):
  args=dict(WindowReport.args,runtime_sampling=False,usb_qmp_lease=True,fast_handshake=True,handshake_ms=50,warmup_ms=100)
  header=HEADER.replace('service_us=10000','service_us=50000').replace('warmup_us=0','warmup_us=100000')
  text=header+'\n'.join(l for l in LOG.splitlines() if 'aud-tpl-sampler: ' not in l and 'aud-tpl-sample: ' not in l)
  # This is a composed parser fixture, not hardware evidence.
  extra='aud-runtime-deadline: phase=typed-complete elapsed_us=530000 limit_us=600000 within=1 includes_final_thermal=1 round_only=0\n'
  extra+='aud-runtime-deadline: phase=runtime-exit elapsed_us=531000 limit_us=600000 within=1 typed_complete=1 sampler_closed_before_stop=1 sampler_required=0 includes_final_thermal=1 round_only=0\n'
  text='\n'.join(l for l in text.splitlines() if 'aud-runtime-deadline: ' not in l).replace('marble-smccc: PAS_stop ',extra+'marble-smccc: PAS_stop ')
  r=audit.inspect(text,**args);self.assertTrue(r['passed'],r['errors'])
  for old,new in [('elapsed_us=531000','elapsed_us=600000'),('sampler_required=0','sampler_required=1'),('typed_complete=1','typed_complete=0')]:
   self.assertFalse(audit.inspect(text.replace(old,new),**args)['passed'],old)
  self.assertFalse(audit.inspect(text+'\naud-tpl-sampler: prepared=1',**args)['passed'])
if __name__=='__main__':unittest.main()
