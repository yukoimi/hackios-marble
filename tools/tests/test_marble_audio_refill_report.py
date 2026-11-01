"""Composed parser fixtures for finite refill, never physical acceptance."""
import importlib.util,json,re,unittest
from pathlib import Path
HERE=Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location('refill_audit',HERE.parent/'marble-audio-owner-report.py')
audit=importlib.util.module_from_spec(spec);spec.loader.exec_module(audit)
ORACLE=json.loads((HERE/'fixtures/audio-oracle-refill8.json').read_text())
def fixture():
 text=(HERE/'fixtures/marble-audio-board-held-v237.log').read_bytes().split(b'\0PMOC')[0].decode(errors='replace')
 lines=text.splitlines();feed=next(x for x in lines if 'marble-audio-owner-feed: ' in x);lines.remove(feed)
 feed=feed.replace('frames=2048','frames=1792')
 for i,line in enumerate(lines):
  if 'marble-audio-owner-PCM: ' in line:
   period=int(re.search(r'period=(\d+)',line)[1]);want=ORACLE['pages'][period]
   for key in ('peak','nonzero','hash'):line=re.sub(r'\b'+key+r'=\w+',key+'='+str(want[key]),line)
   lines[i]=line
 at=next(i for i,x in enumerate(lines) if 'marble-audio-owner-PCM: period=3 ' in x)
 lines[at:at]=[
  'marble-audio-refill: phase=waiting ABI=MAUDIO-REFILL8-MUTED-v1 submitted=3 completed=3 pending=0 feed=512 consumed=512 silence=256 queued=0 audible=0',
  'marble-audio-refill-test: phase=observed-empty submitted=3 completed=3 feed=512 consumed=512 silence=256 queued=0 pending=0 audible=0',feed,
  'marble-audio-refill: phase=resumed feed=1792 queued=1280 consumed=512 silence=256 pending=0 audible=0']
 lines+=['marble-audio-refill-test: phase=retired PASS=1 input_frames=1792 output_frames=2048 periods=8 consumed=1792 silence=256 queued=0 RAM_copy=1 HAL_sink=0 audible=0']
 return '\n'.join(lines)
class RefillReport(unittest.TestCase):
 args=dict(board=True,periods=8,pcm=True,board_held=True,usb_poll_lease=True,refill=True,signal_oracle=ORACLE)
 def test_two_real_native_link_refill_runs(self):
  for name in ('marble-audio-refill-v241.log','marble-audio-refill-v241b.log'):
   text=(HERE/'fixtures'/name).read_bytes().split(b'\0PMOC')[0].decode(errors='replace')
   result=audit.inspect(text,**dict(self.args,qmp_close_snapshot=True,native_link_only=True))
   self.assertTrue(result['passed'],result['errors']);self.assertTrue(result['underrun_recovered']);self.assertFalse(result['continuous_playback'])
 def test_composed_finite_refill(self):
  r=audit.inspect(fixture(),**self.args)
  self.assertTrue(r['passed'],r['errors']);self.assertTrue(r['live_refill']);self.assertTrue(r['underrun_recovered']);self.assertFalse(r['continuous_playback'])
 def test_real_refill_data_does_not_hide_failed_QMP_close(self):
  text=(HERE/'fixtures/marble-audio-refill-v238b-failed.log').read_bytes().split(b'\0PMOC')[0].decode(errors='replace')
  r=audit.inspect(text,**self.args)
  self.assertFalse(r['passed']);self.assertTrue(r['PCM_data_verified']);self.assertTrue(r['signal_oracle_matched']);self.assertFalse(r['live_refill'])
  self.assertNotIn('live refill producer/consumer ordering',r['errors'])
 def test_coherent_QMP_close_requires_bounds_ACKs_and_context(self):
  text=fixture()+'\naud-qmp-close-snapshot: PASS=1 reads=6 unstable=0 failed=0 close_acks=2 elapsed_us=2400 limit_us=750000 double_read=1 transition_validator=unchanged remote_owned_writes=0\n'
  args=dict(self.args,qmp_close_snapshot=True)
  r=audit.inspect(text,**args);self.assertTrue(r['passed'],r['errors'])
  for old,new in [('elapsed_us=2400','elapsed_us=750000'),('reads=6','reads=5'),('failed=0 close_acks=2','failed=3 close_acks=1'),
                  ('transition_validator=unchanged','transition_validator=relaxed'),('descriptor_restored=1','descriptor_restored=0'),('remote_owned_writes=0','remote_owned_writes=1')]:
   self.assertFalse(audit.inspect(text.replace(old,new),**args)['passed'],(old,new))
 def test_native_only_link_gate_does_not_infer_DSP_connection(self):
  text=fixture()+'\naud-qmp-close-snapshot: PASS=1 reads=6 unstable=0 failed=0 close_acks=2 elapsed_us=2400 limit_us=750000 double_read=1 transition_validator=unchanged remote_owned_writes=0\n'
  text='\n'.join(l for l in text.splitlines() if 'marble-glink: register ' not in l and 'marble-glink: deregister ' not in l)
  text=text.replace('marble-pil-boot: auth ', 'aud-owner-link-gate: PASS=1 phase=pre-AUTH XNU_READY=1 EFI_register_calls=0 EFI_deregister_calls=0 FIFO_owner=XNU DSP_link_inferred=0\nmarble-pil-boot: auth ')
  args=dict(self.args,qmp_close_snapshot=True,native_link_only=True)
  result=audit.inspect(text,**args);self.assertTrue(result['passed'],result['errors'])
  for old,new in [('DSP_link_inferred=0','DSP_link_inferred=1'),('XNU_READY=1','XNU_READY=0'),('EFI_register_calls=0','EFI_register_calls=1')]:
   self.assertFalse(audit.inspect(text.replace(old,new),**args)['passed'],(old,new))
  self.assertFalse(audit.inspect(text+'\nmarble-glink: register exact_abi=1',**args)['passed'])
 def test_no_oracle_or_wrong_profile(self):
  for change in [dict(signal_oracle=None),dict(board_held=False),dict(pcm=False)]:
   self.assertFalse(audit.inspect(fixture(),**dict(self.args,**change))['passed'])
 def test_legacy_hardware_log_is_not_refill(self):
  text=(HERE/'fixtures/marble-audio-board-held-v237.log').read_bytes().split(b'\0PMOC')[0].decode(errors='replace')
  self.assertFalse(audit.inspect(text,**self.args)['passed'])
 def test_missing_order_counts_gap_and_cleanup(self):
  text=fixture()
  for old,new in [('phase=resumed feed=1792','phase=resumed feed=2048'),('silence=256','silence=0'),
                  ('completed=3 pending=0','completed=2 pending=1'),('queued=1280','queued=1024'),
                  ('RAM_copy=1','RAM_copy=0'),('phase=quiet-ACK flags=7','phase=quiet-ACK flags=3')]:
   self.assertFalse(audit.inspect(text.replace(old,new),**self.args)['passed'],(old,new))
  lines=text.splitlines();i=next(i for i,x in enumerate(lines) if 'phase=waiting ABI=' in x);j=next(i for i,x in enumerate(lines) if 'phase=resumed feed=' in x)
  lines[i],lines[j]=lines[j],lines[i];self.assertFalse(audit.inspect('\n'.join(lines),**self.args)['passed'])
if __name__=='__main__':unittest.main()
