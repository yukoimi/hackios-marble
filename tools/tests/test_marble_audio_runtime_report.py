"""Composed runtime parser evidence, no hardware claim."""
import unittest
from test_marble_audio_stream_window_report import fixture, WindowReport, audit
class RuntimeReport(unittest.TestCase):
 def text(self):
  extra='aud-runtime-deadline: phase=typed-complete elapsed_us=530000 limit_us=600000 within=1 includes_final_thermal=1 round_only=0\n'
  extra+='aud-tpl-sampler: prepared=1 armed=1 released=1 active=0 count=1 truncated=0 arm_status=0 cancel_status=0 close_status=0 release_us=3 period_100ns=100000 notify_tpl=16 foreign_timer_changes=0 callback_IO=0\n'
  extra+='aud-tpl-sample: index=0 at_us=10000 valid=1 pending=0 next=fe000000 next_callback=fe001000\n'
  extra+='aud-runtime-deadline: phase=runtime-exit elapsed_us=531000 limit_us=600000 within=1 typed_complete=1 sampler_closed_before_stop=1 includes_final_thermal=1 round_only=0\n'
  return fixture().replace('marble-smccc: PAS_stop ',extra+'marble-smccc: PAS_stop ')
 def test_actual_bound_cleanup_and_scope(self):
  args=dict(WindowReport.args,runtime_sampling=True)
  text=self.text();r=audit.inspect(text,**args);self.assertTrue(r['passed'],r['errors'])
  for old,new in [('elapsed_us=531000','elapsed_us=600000'),('elapsed_us=530000','elapsed_us=1529933'),('released=1','released=0'),('sampler_closed_before_stop=1','sampler_closed_before_stop=0'),('within=1','within=0'),('active=0 count=1','active=1 count=1')]:
   self.assertFalse(audit.inspect(text.replace(old,new),**args)['passed'],(old,new))
  self.assertFalse(audit.inspect(fixture(),**args)['passed'])
 def test_QMP_pause_scope_and_stop_deadline(self):
  args=dict(WindowReport.args,runtime_sampling=True,usb_qmp_lease=True)
  text=self.text().replace('scope=PAS-XNU-unmap-free-v2','scope=PAS-XNU-unmap-free-QMP-v3')
  lines=text.splitlines();lease=next(l for l in lines if 'aud-usb-poll-lease: ' in l);lines.remove(lease)
  at=next(i for i,l in enumerate(lines) if 'marble-qmp-hold: complete ' in l);lines.insert(at+1,lease);text='\n'.join(lines)
  r=audit.inspect(text,**args);self.assertTrue(r['passed'],r['errors'])
  for old,new in [('scope=PAS-XNU-unmap-free-QMP-v3','scope=PAS-XNU-unmap-free-v2'),('hold_us=610990','hold_us=1000000'),('elapsed_us=611088','elapsed_us=1000000'),('marble-qmp-hold: complete PASS=1','marble-qmp-hold: complete PASS=0')]:
   self.assertNotEqual(text,text.replace(old,new),old)
   self.assertFalse(audit.inspect(text.replace(old,new),**args)['passed'],old)
  self.assertFalse(audit.inspect(text,**dict(args,usb_qmp_lease=False))['passed'])
if __name__=='__main__':unittest.main()
