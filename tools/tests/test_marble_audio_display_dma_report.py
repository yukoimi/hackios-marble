"""Composed parser fixtures only; actual DMA fields remain firmware checked."""
import re,unittest
from pathlib import Path
from test_marble_audio_fast_handshake_report import LOG
from test_marble_audio_stream_window_report import WindowReport,audit
MODE='marble-display-memory: mode=audio-DMA-only bus_lease=1 timing_qualified=0 samples=2 fields=66 interval_us=20000\n'
TEXT=LOG.replace('marble-display-memory: begin ',MODE+'marble-display-memory: begin ')
ARGS=dict(WindowReport.args,runtime_sampling=True,usb_qmp_lease=True,display_dma_only=True)
class DisplayDMAReport(unittest.TestCase):
 def test_real_two_round_closure(self):
  args=dict(WindowReport.args,runtime_sampling=False,usb_qmp_lease=True,fast_handshake=True,handshake_ms=100,warmup_ms=100,prelaunch=True,display_dma_only=True)
  for name in ('v259','v259b'):
   text=(Path(__file__).parent/'fixtures'/('marble-audio-display-'+name+'.log')).read_text(errors='replace')
   r=audit.inspect(text,**args);self.assertTrue(r['passed'],(name,r['errors']));self.assertFalse(r['continuous_playback']);self.assertFalse(r['HAL_sink']);self.assertFalse(r['physical_output'])
 def test_complete(self):
  r=audit.inspect(TEXT,**ARGS);self.assertTrue(r['passed'],r['errors']);self.assertFalse(r['physical_output'])
 def test_missing_mode_or_unexpected_profile(self):
  self.assertFalse(audit.inspect(LOG,**ARGS)['passed'])
  self.assertFalse(audit.inspect(TEXT,**dict(ARGS,display_dma_only=False))['passed'])
  self.assertFalse(audit.inspect(TEXT,**dict(ARGS,board=False))['passed'])
 def test_incomplete_register_samples(self):
  for pattern in [r'^marble-display-memory: sample=0 dspp=1 .*\n',r'^marble-display-memory: sample=1 pipe=7 .*\n',r'^marble-display-memory: complete .*\n']:
   bad=re.sub(pattern,'',TEXT,count=1,flags=re.M);self.assertNotEqual(bad,TEXT)
   self.assertFalse(audit.inspect(bad,**ARGS)['passed'])
 def test_dirty_overlap_unstable_and_missing_isolation_rejected(self):
  for old,new in [('bus_restored=1 audit_only','bus_restored=0 audit_only'),('complete stable=1','complete stable=0'),('conservative_overlap=0','conservative_overlap=1'),('fetch_enabled=0','fetch_enabled=1'),('exact_single_GOP_disjoint=1','exact_single_GOP_disjoint=0'),('fields=66','fields=64'),('interval_us=20000','interval_us=0')]:
   bad=TEXT.replace(old,new,1);self.assertNotEqual(bad,TEXT,old)
   self.assertFalse(audit.inspect(bad,**ARGS)['passed'],old)
if __name__=='__main__':unittest.main()
