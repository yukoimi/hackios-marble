"""Reject incomplete or unsafe cross-core closure using real v213 evidence."""
import importlib.util,unittest,json,re
from pathlib import Path
HERE=Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location('owner_report',HERE.parent/'marble-audio-owner-report.py')
audit=importlib.util.module_from_spec(spec);spec.loader.exec_module(audit)
LOG=(HERE/'fixtures/marble-audio-owner-v213.log').read_text()

class OwnerAudit(unittest.TestCase):
 def test_real_v234_full_stop_and_later_USB_progress(self):
  text=(HERE/'fixtures/marble-audio-owner-usb-v234.log').read_text()
  r=audit.inspect(text,board=True,periods=8,pcm=True,usb_poll_lease=True)
  self.assertTrue(r['passed'],r['errors']);self.assertTrue(r['PCM_data_verified']);self.assertFalse(r['audio_acceptance'])
  for old,new in [('elapsed_us=611066','elapsed_us=1410916'),('scope=PAS-XNU-unmap-free-v2','scope=PAS-only'),('scheduler_progress=1','scheduler_progress=0')]:
   self.assertFalse(audit.inspect(text.replace(old,new),board=True,periods=8,pcm=True,usb_poll_lease=True)['passed'])
 def test_real_v233c_raw_PAS_success_is_not_whole_stop_success(self):
  text=(HERE/'fixtures/marble-audio-owner-usb-v233c-failed.log').read_text()
  r=audit.inspect(text,board=True,periods=8,pcm=True,usb_poll_lease=True)
  self.assertFalse(r['passed']);self.assertTrue(r['PCM_data_verified']);self.assertFalse(r['audio_acceptance'])
 def test_USB_poll_lease_requires_explicit_profile_and_original_bound(self):
  # Composed parser fixture, not physical proof of the new lifecycle.
  text=(HERE/'fixtures/marble-audio-owner-v222.log').read_text()
  text='aud-usb-poll-preflight: PASS=1 timer_writes=0 DSP_start=0\n'+text
  lease='aud-usb-poll-lease: scope=PAS-XNU-unmap-free-v2 qualified=1 attempted=1 cancelled=1 resume_attempted=1 restored=1 failed=0 event=fc03db98 callback=fbefef70 period_100ns=1000000 hold_us=608000 cancel_status=0 resume_status=0 cadence_rebased=1 timer_API_only=1 CloseEvent_calls=0 pending_removals=0 charger_writes=0 IRQ_changes=0 full_PAS_limit_us=1000000\n'
  lease+='marble-pil-PAS: phase=metadata-shutdown status=0 transport=1 secure=0 elapsed_us=610000 dirty=0 PASS=1 auth_reset=1 dsp_start=1\n'
  text=text.replace('marble-pil-held: restored ',lease+'marble-pil-held: restored ')
  text+='\naud-usb-poll-later: PASS=1 checked=1 binding=1 period_100ns=1000000 resume_trigger=1000000 later_trigger=2000000 notify_link=0 timer_link=fc03dc30 scheduler_progress=1 timer_writes=0 callback_calls=0\n'
  args=dict(board=True,periods=8,pcm=True,usb_poll_lease=True)
  self.assertTrue(audit.inspect(text,**args)['passed'])
  self.assertFalse(audit.inspect(text,board=True,periods=8,pcm=True)['passed'])
  for old,new in [('cancelled=1','cancelled=0'),('restored=1 failed=0','restored=0 failed=1'),('resume_status=0','resume_status=7'),('hold_us=608000','hold_us=1000000'),('charger_writes=0','charger_writes=1'),('pending_removals=0','pending_removals=1'),('period_100ns=1000000','period_100ns=2000000'),('later_trigger=2000000','later_trigger=1000000'),('notify_link=0','notify_link=fc03dbe0'),('elapsed_us=610000','elapsed_us=1410916')]:
   self.assertFalse(audit.inspect(text.replace(old,new),**args)['passed'],(old,new))
 def test_sampler_requires_exact_cleanup_and_valid_bounded_samples(self):
  header='aud-tpl-sampler: prepared=1 armed=1 released=1 active=0 count=1 truncated=0 arm_status=0 cancel_status=0 close_status=0 release_us=3 period_100ns=100000 notify_tpl=16 foreign_timer_changes=0 callback_IO=0\n'
  sample='aud-tpl-sample: index=0 at_us=10000 valid=1 pending=10100 next=fe7de6e0 next_callback=fee19794\n'
  text=LOG+'\n'+header+sample
  self.assertTrue(audit.inspect(text,tpl_sampling=True)['passed'])
  for old,new in [('released=1','released=0'),('active=0','active=1'),('truncated=0','truncated=1'),('arm_status=0','arm_status=7'),('cancel_status=0','cancel_status=7'),('close_status=0','close_status=7'),('count=1','count=128'),('index=0','index=1'),('valid=1','valid=0'),('at_us=10000','at_us=2000000')]:
   self.assertFalse(audit.inspect(LOG+'\n'+(header+sample).replace(old,new),tpl_sampling=True)['passed'],(old,new))
  self.assertFalse(audit.inspect(text+sample,tpl_sampling=True)['passed'])
  self.assertFalse(audit.inspect(LOG,tpl_sampling=True)['passed'])
  self.assertTrue(audit.inspect(LOG+'\n'+header.replace('count=1','count=0'),tpl_sampling=True)['passed'])
 def test_real_PCM_data_requires_all_pages_and_independent_first_check(self):
  text=(HERE/'fixtures/marble-audio-owner-v222.log').read_text()
  r=audit.inspect(text,board=True,periods=8,pcm=True);self.assertTrue(r['passed'],r['errors']);self.assertTrue(r['bounded_PCM'])
  self.assertFalse(r['audio_acceptance']);self.assertFalse(r['HAL_sink'])
  for line in text.splitlines(keepends=True):
   self.assertFalse(audit.inspect(text.replace(line,''),board=True,periods=8,pcm=True)['passed'],line)
   self.assertFalse(audit.inspect(text+line,board=True,periods=8,pcm=True)['passed'],line)
 def test_signal_oracle_rejects_silence_corruption_and_wrong_shape(self):
  text=(HERE/'fixtures/marble-audio-owner-v222.log').read_text()
  oracle=json.loads((HERE/'fixtures/audio-oracle-pcm8.json').read_text())
  self.assertFalse(audit.inspect(text,board=True,periods=8,pcm=True,signal_oracle=oracle)['passed'])
  # Parser model: inject independently generated oracle rows; not hardware evidence.
  pages=iter(oracle['pages'])
  def row(match):
   p=next(pages);return f"marble-audio-owner-PCM: period={p['period']} peak={p['peak']} nonzero={p['nonzero']} hash={p['hash']} bytes=1024 tail_zero=1 audible=0"
  text=re.sub(r'marble-audio-owner-PCM: [^\r\n]+',row,text)
  p=oracle['pages'][0]
  text=re.sub(r'aud-owner-PCM-first: [^\r\n]+',f"aud-owner-PCM-first: peak={p['peak']} nonzero={p['nonzero']} hash={p['hash']} bytes=1024 tail_zero=1 audible=0",text)
  peak=max(p['peak'] for p in oracle['pages']);text=text.replace('frames=2048 peak=1 guard=original',f'frames=2048 peak={peak} guard=original')
  r=audit.inspect(text,board=True,periods=8,pcm=True,signal_oracle=oracle);self.assertTrue(r['passed'],r['errors']);self.assertTrue(r['signal_oracle_matched'])
  for changed in (None,[],dict(oracle,pages=None),dict(oracle,pages=['bad']*8),dict(oracle,rate=96000)):
   if changed is None:continue
   self.assertFalse(audit.inspect(text,board=True,periods=8,pcm=True,signal_oracle=changed)['passed'])
  corrupted=text.replace('hash='+oracle['pages'][4]['hash'],'hash=0',1)
  self.assertFalse(audit.inspect(corrupted,board=True,periods=8,pcm=True,signal_oracle=oracle)['passed'])
 def test_real_eight_periods_rejects_two_period_interpretation(self):
  text=(HERE/'fixtures/marble-audio-owner-v219.log').read_text()
  self.assertTrue(audit.inspect(text,board=True,periods=8)['passed'])
  self.assertFalse(audit.inspect(text,board=True)['passed'])
  self.assertFalse(audit.inspect(text,board=True,periods=8,pcm=True)['passed'])
 def test_PCM_model_requires_bounded_real_device_data_and_first_page_match(self):
  # Composed parser fixture only; actual PCM hardware acceptance is separate.
  text=(HERE/'fixtures/marble-audio-owner-v219.log').read_text()
  text=text.replace('MAUDIO-OWNER-XNU-ZERO8-v1','MAUDIO-OWNER-XNU-PCM8-MUTED-v1').replace('GLINK_owner=XNU zero=1','GLINK_owner=XNU zero=0').replace('RX=154 TX=35 zero=1','RX=154 TX=35 zero=0').replace('profile=muted-Music zero_only=1','profile=muted-Music-PCM8 zero_only=0')
  first='aud-owner-PCM-first: peak=1 nonzero=74 hash=abcdef bytes=1024 tail_zero=1 audible=0\n'
  pos=text.index('aud-owner-fw: phase=configured-real-START-proof ');text=text[:pos]+first+text[pos:]
  text+='marble-audio-owner-feed: source=redwood-device-write frames=2048 peak=1 guard=original HPF=1 gain_db=0 extra_divisor=16 peak_bound=64 HAL_sink=0 audible=0\n'
  for i in range(8):text+=f'marble-audio-owner-PCM: period={i} peak={1 if i==0 else 0} nonzero={74 if i==0 else 0} hash=abcdef bytes=1024 tail_zero=1 audible=0\n'
  r=audit.inspect(text,board=True,periods=8,pcm=True);self.assertTrue(r['passed'],r['errors'])
  for old,new in [('peak=1','peak=65'),('tail_zero=1','tail_zero=0'),('guard=original','guard=none'),('nonzero=74','nonzero=0'),('hash=abcdef bytes=1024 tail_zero=1 audible=0\n','hash=123456 bytes=1024 tail_zero=1 audible=0\n')]:
   changed=text.replace(old,new,1);self.assertFalse(audit.inspect(changed,board=True,periods=8,pcm=True)['passed'])
  self.assertFalse(audit.inspect(text,board=True,periods=8)['passed'])
 def test_real_v217_muted_board(self):
  text=(HERE/'fixtures/marble-audio-owner-v217.log').read_text()
  r=audit.inspect(text,board=True);self.assertTrue(r['passed'],r['errors']);self.assertTrue(r['board_configured'])
  self.assertFalse(r['audio_acceptance']);self.assertFalse(r['continuous_playback'])
  self.assertFalse(audit.inspect(text)['passed'])
  for line in text.splitlines(keepends=True):
   self.assertFalse(audit.inspect(text.replace(line,''),board=True)['passed'],line)
   self.assertFalse(audit.inspect(text+line,board=True)['passed'],line)
 def test_real_cross_core_zero_only(self):
  r=audit.inspect(LOG);self.assertTrue(r['passed'],r['errors'])
  for key in ('audio_acceptance','HAL_sink','physical_output'):self.assertFalse(r[key])
 def test_every_boundary_required_once(self):
  for line in LOG.splitlines(keepends=True):
   self.assertFalse(audit.inspect(LOG.replace(line,''))['passed'],line)
   self.assertFalse(audit.inspect(LOG+line)['passed'],line)
 def test_pas_unmap_free_order(self):
  lines=LOG.splitlines(keepends=True)
  for a,b in [('marble-smccc: PAS_stop ','marble-audio-map: result '),('aud-owner-fw: phase=PAS-off-wait-XNU-unmap ','aud-owner-fw: phase=DMA-released-after-XNU-unmap ')]:
   changed=lines[:];i=next(i for i,l in enumerate(lines) if a in l);j=next(i for i,l in enumerate(lines) if b in l)
   changed[i],changed[j]=changed[j],changed[i]
   self.assertFalse(audit.inspect(''.join(changed))['passed'])
 def test_epoch_mismatch_and_malformed(self):
  line=next(l for l in LOG.splitlines() if 'marble-audio-owner-kernel: ABI=' in l)
  import re
  for value in ('0','xyz','10000000000000000'):
   changed=re.sub(r'epoch=\S+','epoch='+value,line)
   self.assertFalse(audit.inspect(LOG.replace(line,changed))['passed'])
  self.assertFalse(audit.inspect('')['passed'])
 def test_fault_and_thermal_stops(self):
  for suffix in ('marble-thermal: STOP requested','marble-cpucap: runtime request changed','panic(cpu','ACK-missing-retain','tr: q ','marble-aw-digital: complete '):
   self.assertFalse(audit.inspect(LOG+'\n'+suffix)['passed'])
 def test_unmap_ack_and_single_fifo_owner(self):
  for old,new in [('accepted_sequence=8','accepted_sequence=6'),('EFI_FIFO_writes=0','EFI_FIFO_writes=1'),('DMA_unmapped=1','DMA_unmapped=0'),('PAS_stopped=1','PAS_stopped=0'),('freed=1','freed=0')]:
   self.assertIn(old,LOG);self.assertFalse(audit.inspect(LOG.replace(old,new))['passed'])
 def test_board_requires_all_muted_hardware_evidence(self):
  self.assertFalse(audit.inspect(LOG,board=True)['passed'])
  # A composed audit fixture, not a claim that the combined board ran.
  old=(HERE/'fixtures/marble-audio-native-v209.log').read_text()
  take=lambda prefix:next(l for l in old.splitlines(keepends=True) if l.startswith(prefix))
  config=take('tp: acquired ')
  for device in ('34','35'):
   for kind in ('analog','digital'):config+=take(f'marble-aw-{kind}: complete device={device} ')
  config+=take('aw: window end ')
  composed=LOG.replace('profile=disconnected zero_only=1 TDM_pin_writes=0 amplifier_commands=0','profile=muted-Music zero_only=1 TDM_pin_writes=1 amplifier_commands=1')
  pos=composed.index('aud-owner-board:');composed=composed[:pos]+config+composed[pos:]
  pos=composed.index('marble-pil-boot: observed ');composed=composed[:pos]+take('tp: end ')+composed[pos:]
  pos=composed.index('aud-owner-session:');composed=composed[:pos]+take('aw: end ')+composed[pos:]
  r=audit.inspect(composed,board=True);self.assertTrue(r['passed'],r['errors'])
  for old,new in [('HMUTE_retained=1','HMUTE_retained=0'),('speaker_switch_writes=0','speaker_switch_writes=1'),('attenuation_db=90','attenuation_db=0'),('window_passed=1','window_passed=0')]:
   self.assertFalse(audit.inspect(composed.replace(old,new),board=True)['passed'])

if __name__=='__main__':unittest.main()
