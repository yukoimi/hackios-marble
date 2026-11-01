"""Composed stream24 parser fixture; not a physical acceptance record."""
import importlib.util,json,re,unittest
from pathlib import Path
HERE=Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location('window_audit',HERE.parent/'marble-audio-owner-report.py')
audit=importlib.util.module_from_spec(spec);spec.loader.exec_module(audit)
ORACLE=json.loads((HERE/'fixtures/audio-oracle-stream24.json').read_text())
def fixture():
 text=(HERE/'fixtures/marble-audio-refill-v241.log').read_bytes().split(b'\0PMOC')[0].decode(errors='replace')
 text=re.sub(r'\b(submitted|completed|published)=8\b',r'\g<1>=24',text)
 text=text.replace('PCM_requests=8','PCM_requests=24').replace('muted-Music-PCM8','muted-Music-PCM24')
 text=text.replace('MAUDIO-OWNER-XNU-PCM8-MUTED-v1','MAUDIO-OWNER-XNU-STREAM24-MUTED-v2').replace('MAUDIO-BOARD-QUIET-v1','MAUDIO-BOARD-QUIET24-v2')
 text=text.replace('marble-audio-refill:','marble-audio-stream:').replace('marble-audio-refill-test:','marble-audio-stream-test:').replace('MAUDIO-REFILL8-MUTED-v1','MAUDIO-STREAM24-MUTED-v2')
 text=text.replace('phase=resumed feed=1792 queued=1280','phase=resumed feed=1536 queued=1024')
 text=text.replace('frames=1792','frames=5888').replace('consumed=1792','consumed=5888').replace('output_frames=2048','output_frames=6144').replace('periods=8','periods=24')
 text=text.replace('queued=0 RAM_copy=1 HAL_sink=0','queued=0 RAM_copy=1 flow_control=1 writes=19 HAL_sink=0').replace('RX=154 TX=35','RX=186 TX=67')
 lines=text.splitlines();feed=next(l for l in lines if 'marble-audio-owner-feed: ' in l);lines.remove(feed)
 pos=next(i for i,l in enumerate(lines) if 'marble-audio-owner-PCM: period=0 ' in l)
 lines=[l for l in lines if 'marble-audio-owner-PCM: period=' not in l]
 # Keep the preexisting waiting/resumed lines between period two and three.
 before=next(i for i,l in enumerate(lines) if 'marble-audio-stream: phase=waiting ' in l)
 def page(p):return 'marble-audio-owner-PCM: period={period} peak={peak} nonzero={nonzero} hash={hash} bytes=1024 tail_zero=1 audible=0'.format(**p)
 lines[before:before]=[page(p) for p in ORACLE['pages'][:3]]
 after=next(i for i,l in enumerate(lines) if 'marble-audio-stream: phase=resumed ' in l)+1
 lines[after:after]=[page(p) for p in ORACLE['pages'][3:23]]+[feed,page(ORACLE['pages'][23])]
 at=next(i for i,l in enumerate(lines) if 'marble-audio-owner-kernel: phase=retired ' in l)
 lines[at:at]=['marble-audio-stream: phase=retired input_frames=5888 output_frames=6144 periods=24 consumed=5888 silence=256 queued=0 head=5888 tail=5888 wraps=1 writes=19 max_queued=1536 HAL_sink=0 audible=0']
 return '\n'.join(lines)
class WindowReport(unittest.TestCase):
 args=dict(board=True,periods=24,pcm=True,board_held=True,usb_poll_lease=True,stream_window=True,qmp_close_snapshot=True,native_link_only=True,signal_oracle=ORACLE)
 def test_composed_complete(self):
  r=audit.inspect(fixture(),**self.args);self.assertTrue(r['passed'],r['errors']);self.assertTrue(r['stream_window_verified']);self.assertFalse(r['continuous_playback'])
 def test_missing_or_mixed_profile(self):
  for change in [dict(stream_window=False),dict(refill=True),dict(native_link_only=False),dict(periods=8),dict(signal_oracle=None)]:
   self.assertFalse(audit.inspect(fixture(),**dict(self.args,**change))['passed'],change)
 def test_missing_wrap_wrong_peak_extra_gap_or_watermark(self):
  text=fixture()
  for old,new in [('wraps=1','wraps=0'),('max_queued=1536','max_queued=4096'),('writes=19','writes=18'),
                  ('head=5888 tail=5888','head=5888 tail=5632'),('silence=256','silence=512'),('phase=resumed feed=1536 queued=1024','phase=resumed feed=1536 queued=512'),
                  ('period=23 peak=30','period=23 peak=64'),('submitted=24 completed=24','submitted=24 completed=23')]:
   self.assertNotEqual(text,text.replace(old,new),old)
   self.assertFalse(audit.inspect(text.replace(old,new),**self.args)['passed'],(old,new))
 def test_eight_period_hardware_is_not_v2_window(self):
  text=(HERE/'fixtures/marble-audio-refill-v241.log').read_bytes().split(b'\0PMOC')[0].decode(errors='replace')
  self.assertFalse(audit.inspect(text,**self.args)['passed'])
if __name__=='__main__':unittest.main()
