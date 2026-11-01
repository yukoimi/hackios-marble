"""One bounded black RAM-only XNU audio owner test, then Android screen off."""
from marble_local import ADB as MARBLE_ADB, ANDROID_SERIAL as MARBLE_ANDROID_SERIAL, FASTBOOT as MARBLE_FASTBOOT
import argparse,hashlib,json,re,subprocess,time
from pathlib import Path
from marble_android import battery_snapshot,screen_off
from marble_audio_refill_profile import DISK_SHA256 as REFILL_DISK_SHA, ORACLE_SHA256 as REFILL_ORACLE_SHA, STREAM_DISK_SHA256 as STREAM_DISK_SHA, STREAM_ORACLE_SHA256 as STREAM_ORACLE_SHA
ROOT=Path(__file__).resolve().parents[1]
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--out',type=Path,required=True)
 p.add_argument('--owner-package',type=Path,help='pinned MADSP001 package; explicit combined XNU owner smoke')
 p.add_argument('--owner-periods',type=int,choices=(2,8,24),default=2)
 p.add_argument('--owner-native-link',action='store_true',help='require native XNU-only link gate without legacy EFI registration')
 p.add_argument('--owner-qmp-close-snapshot',action='store_true',help='require coherent QMP close evidence inside original 750ms deadline')
 p.add_argument('--owner-stream',action='store_true',help='separate v2 muted 24-period producer/wrap test')
 p.add_argument('--owner-refill',action='store_true',help='finite live producer with one deliberate silence page and later refill')
 p.add_argument('--board-held',action='store_true',help='explicit muted amplifier hold with drained/quiet STOP handshake')
 p.add_argument('--owner-pcm',action='store_true',help='require bounded guarded device-write PCM8 evidence; amps remain muted')
 p.add_argument('--owner-signal-oracle',type=Path,help='pinned independent triangle1k original-guard oracle; requires PCM mode and matching RAM disk')
 p.add_argument('--owner-display-dma-only',action='store_true',help='require explicit audio DMA audit without panel timing dependency')
 p.add_argument('--owner-prelaunch',action='store_true',help='require early single CPU_ON and deferred cap with original100ms DSP cadence')
 p.add_argument('--owner-warmup-ms',type=int,choices=(0,100),default=0)
 p.add_argument('--owner-handshake-ms',type=int,choices=(10,50,100),default=10)
 p.add_argument('--owner-fast-handshake',action='store_true',help='require explicit 10ms real-response startup service profile')
 p.add_argument('--owner-usb-qmp-lease',action='store_true',help='require QMP closure before poll resume inside original 1s outer bound')
 p.add_argument('--owner-runtime-sampling',action='store_true',help='require runtime sampler cleanup and strict actual runtime deadline')
 p.add_argument('--owner-tpl-sampling',action='store_true',help='also require own TPL software timer cancellation/closure and bounded RAM snapshots')
 p.add_argument('--owner-usb-poll-lease',action='store_true',help='require explicit pinned USB poll pause/resume proof inside original PAS bound')
 p.add_argument('--owner-board',action='store_true',help='require the explicit muted Music/TDM board closure in addition to XNU ownership')
 p.add_argument('--native-transport',action='store_true',help='also require readonly native SMEM discovery; no DSP start')
 p.add_argument('--plan',type=Path,default=Path('out/audio-queue-v195-plan'))
 p.add_argument('--boot-image',type=Path,default=Path('out/marble-vsync-daily-v11-loader-boot.img'));a=p.parse_args()
 if a.owner_board and not a.owner_package:p.error('--owner-board requires --owner-package')
 if a.owner_pcm and not (a.owner_board and (a.owner_periods==8 or (a.owner_stream and a.owner_periods==24))):p.error('--owner-pcm requires muted board and eight periods')
 if a.owner_native_link and not a.owner_qmp_close_snapshot:p.error('--owner-native-link requires coherent QMP close')
 if a.owner_qmp_close_snapshot and not (a.owner_refill or a.owner_stream):p.error('--owner-qmp-close-snapshot requires live refill')
 if a.owner_stream and not (a.owner_periods==24 and a.owner_pcm and a.board_held and a.owner_native_link and a.owner_qmp_close_snapshot and a.owner_signal_oracle and not a.owner_refill):p.error('--owner-stream requires explicit held native PCM24 and independent oracle')
 if a.owner_periods==24 and not a.owner_stream:p.error('24 periods require explicit v2 stream mode')
 if a.owner_refill and not (a.board_held and a.owner_signal_oracle):p.error('--owner-refill requires held board and explicit oracle')
 if a.board_held and not a.owner_pcm:p.error('--board-held requires --owner-pcm')
 if a.owner_signal_oracle and not a.owner_pcm:p.error('--owner-signal-oracle requires --owner-pcm')
 if a.owner_display_dma_only and not (a.owner_board and a.owner_stream and a.owner_periods==24):p.error('--owner-display-dma-only requires muted owner-board stream24')
 if a.owner_prelaunch and not (a.owner_fast_handshake and a.owner_handshake_ms==100 and a.owner_warmup_ms==100):p.error('--owner-prelaunch requires explicit original100ms cadence')
 if a.owner_warmup_ms and not a.owner_fast_handshake:p.error('--owner-warmup-ms requires handshake profile')
 if a.owner_fast_handshake and not a.owner_usb_qmp_lease:p.error('--owner-fast-handshake requires QMP lease')
 if a.owner_usb_qmp_lease and not (a.owner_usb_poll_lease and a.owner_stream):p.error('--owner-usb-qmp-lease requires stream24/USB profile')
 if a.owner_runtime_sampling and not (a.owner_stream and not a.owner_tpl_sampling):p.error('--owner-runtime-sampling requires stream24 and excludes PAS sampler')
 if a.owner_tpl_sampling and not a.owner_board:p.error('--owner-tpl-sampling requires --owner-board')
 if a.owner_usb_poll_lease and (not a.owner_board or a.owner_tpl_sampling):p.error('--owner-usb-poll-lease requires owner board without the diagnostic sampler')
 a.out.mkdir(exist_ok=False);r=dict(scope='real XNU GLINK / EFI resource owner; no HAL or audible output' if a.owner_package else 'synthetic RAM queue probe',audio_acceptance=False,periods=a.owner_periods,board_muted=a.owner_board)
 def save():(a.out/'exercise.json').write_text(json.dumps(r,indent=2)+'\n')
 def call(args,label,timeout=15):
  q=subprocess.run(list(map(str,args)),capture_output=True,timeout=timeout,cwd=ROOT)
  raw=q.stdout+q.stderr;(a.out/(label+'.txt')).write_bytes(raw)
  if q.returncode:raise RuntimeError(label+': '+raw.decode(errors='replace')[-1800:])
  return raw.decode(errors='replace')
 def fb(*args,label,timeout=15):return call([MARBLE_FASTBOOT,'-s','xnuloader',*args],label,timeout)
 rebooted=False
 try:
  _,before=battery_snapshot(a.out,'battery-before');r['battery_before_c']=before['temperature_c']
  screen_off(a.out);r['android_screen_off_before']=True
  if not 0<=before['temperature_c']<38:raise RuntimeError('battery preflight')
  # Android's tenth-degree reading and the ROM's whole-degree ADC can differ.
  # Leave extra start headroom, preserving the original ROM 38C refusal.
  if a.owner_board and before['temperature_c']>36:raise RuntimeError('board test needs Android battery <=36C before temporary reboot; leave screen off to cool')
  # Validate the complete local bundle before changing the phone's boot state.
  kernel=a.plan/'mach.marble-source';metadata=kernel.with_suffix('.json')
  profile=json.loads(metadata.read_text())
  if profile.get('hardware_status','').startswith('FAILED') or hashlib.sha256(kernel.read_bytes()).hexdigest()!=profile.get('kernel_sha256'):
   raise RuntimeError('local source-kernel metadata/hash preflight')
  for path in (a.plan/'test.apfs',a.boot_image):
   if not path.is_file() or not path.stat().st_size:raise RuntimeError('missing local bundle artifact: '+str(path))
  if a.owner_package and (a.owner_package.stat().st_size!=30180144 or hashlib.sha256(a.owner_package.read_bytes()).hexdigest()!='ce5a2ee6534ea6fe4e5d2272252989854a3e0277893f49c87d63376458f7d237'):
   raise RuntimeError('local owner-package identity preflight')
  oracle=None
  if a.owner_signal_oracle:
   if hashlib.sha256(a.owner_signal_oracle.read_bytes()).hexdigest()!=(STREAM_ORACLE_SHA if a.owner_stream else REFILL_ORACLE_SHA if a.owner_refill else '6ad02888de855b54a55ff9dab836245f3e71a73289720ed3a0247c78ec393502'):raise RuntimeError('signal oracle identity preflight')
   oracle=json.loads(a.owner_signal_oracle.read_text());(a.out/'signal-oracle.json').write_text(json.dumps(oracle,indent=2)+'\n')
  if a.owner_package:
   build_profile=json.loads((a.boot_image.parent/'build-profile.json').read_text())
   if bool(build_profile.get('audio_owner_stream'))!=a.owner_stream:raise RuntimeError('stream24 profile mismatch before reboot')
   if a.owner_stream and b'MAUDIO-OWNER-XNU-STREAM24-MUTED-v2' not in kernel.read_bytes():raise RuntimeError('stream24 kernel ABI absent before reboot')
   if bool(build_profile.get('audio_display_dma_only'))!=a.owner_display_dma_only:raise RuntimeError('audio display DMA audit profile mismatch before reboot')
   if bool(build_profile.get('audio_owner_prelaunch'))!=a.owner_prelaunch:raise RuntimeError('native prelaunch profile mismatch before reboot')
   if a.owner_fast_handshake and build_profile.get('audio_owner_warmup_ms',0)!=a.owner_warmup_ms:raise RuntimeError('native warmup profile mismatch before reboot')
   if a.owner_fast_handshake and build_profile.get('audio_owner_handshake_ms',10)!=a.owner_handshake_ms:raise RuntimeError('native handshake interval mismatch before reboot')
   if bool(build_profile.get('audio_owner_fast_handshake'))!=a.owner_fast_handshake:raise RuntimeError('native handshake profile mismatch before reboot')
   if bool(build_profile.get('audio_usb_qmp_lease'))!=a.owner_usb_qmp_lease:raise RuntimeError('USB QMP lease profile mismatch before reboot')
   if bool(build_profile.get('audio_runtime_sampling'))!=a.owner_runtime_sampling:raise RuntimeError('runtime sampler profile mismatch before reboot')
   if bool(build_profile.get('audio_owner_native_link'))!=a.owner_native_link:raise RuntimeError('native-only link profile mismatch before reboot')
   if bool(build_profile.get('audio_qmp_close_snapshot'))!=a.owner_qmp_close_snapshot:raise RuntimeError('QMP close snapshot profile mismatch before reboot')
   if bool(build_profile.get('audio_owner_refill'))!=a.owner_refill:raise RuntimeError('refill profile mismatch before reboot')
   if a.owner_refill and b'MAUDIO-REFILL8-MUTED-v1' not in kernel.read_bytes():raise RuntimeError('refill ABI missing before reboot')
   if bool(build_profile.get('audio_board_held'))!=a.board_held:raise RuntimeError('board hold profile mismatch before reboot')
   if a.board_held and b'MAUDIO-BOARD-QUIET-v1' not in kernel.read_bytes():raise RuntimeError('board gate kernel ABI missing before reboot')
   if bool(build_profile.get('audio_usb_poll_lease'))!=a.owner_usb_poll_lease:raise RuntimeError('USB poll lease profile mismatch before reboot')
   expected_disk=STREAM_DISK_SHA if a.owner_stream else REFILL_DISK_SHA if a.owner_refill else '8361b8538d88d32cd5f50138c96d06d18262f5c78c77f6d2d8c0a842b9bdf579' if oracle is not None else '82a15700ea358e1f3c492437e71ad9c6a2ffe4c21c8c1635452df6304566494e'
   if hashlib.sha256((a.plan/'test.apfs').read_bytes()).hexdigest()!=expected_disk:raise RuntimeError('owner RAM disk identity preflight')
  r['boot_id_before']=call([MARBLE_ADB,'-s',MARBLE_ANDROID_SERIAL,'shell','cat','/proc/sys/kernel/random/boot_id'],'boot-id-before').strip()
  call([MARBLE_ADB,'-s',MARBLE_ANDROID_SERIAL,'reboot','bootloader'],'reboot-bootloader');rebooted=True;save()
  deadline=time.monotonic()+40
  while time.monotonic()<deadline:
   devices=call([MARBLE_FASTBOOT,'devices'],'bootloader-devices')
   if MARBLE_ANDROID_SERIAL in devices:break
   time.sleep(1)
  else:raise RuntimeError('stock fastboot did not enumerate')
  call(['python','tools/marble-run.py','--serial',MARBLE_ANDROID_SERIAL,'boot','--image',a.boot_image],'temporary-boot',70)
  deadline=time.monotonic()+35
  while time.monotonic()<deadline:
   d=call([MARBLE_FASTBOOT,'devices'],'device-list')
   if 'xnuloader' in d:break
   time.sleep(1)
  else:raise RuntimeError('loader did not enumerate')
  fb('oem','idle-probe',label='idle-probe')
  run=['python','tools/marble-run.py','run','--kernel',a.plan/'mach.marble-source','--ramdisk',a.plan/'test.apfs',
       '--seconds','18','--cpus','1','--blank-display','--audio-queue-probe','--cpu-cap','--efi-idle','--short-thermal','--battery-preflight',a.out/'battery-before.json']
  if a.native_transport:run.append('--audio-transport-probe')
  if a.owner_package:run.extend(['--audio-owner-package',str(a.owner_package),'--audio-owner-periods',str(a.owner_periods)])
  if a.owner_pcm:run.append('--audio-owner-pcm')
  if a.board_held:run.append('--audio-board-held')
  if a.owner_refill:run.append('--audio-owner-refill')
  if a.owner_stream:run.append('--audio-owner-stream')
  if a.owner_native_link:run.append('--audio-owner-native-link')
  if a.owner_fast_handshake:run.append('--audio-owner-blocking-log')
  output=call(run,'kernel-run',85)
  match=re.search(r'log: (.+)',output)
  if not match:raise RuntimeError('returned kernel log path absent')
  source=Path(match[1].strip());raw=source.read_bytes();(a.out/'returned.log').write_bytes(raw)
  text=raw.split(b'\0PMOC')[0].decode(errors='replace')
  expected=['marble-audio-queue: ready ABI=MAUDIO-QUEUE-PROBE-v1 version=1 capacity=4096 rate=48000 bits=16 channels=2 DSP=0 hardware_audio=0',
   'marble-audio-queue-test: invalid_count_rejected=1 DSP=0',
   'marble-audio-queue-test: full_order_and_underflow PASS=1 accepted=4096 dropped=2048 silence=256',
   'marble-audio-queue-test: recycled PASS=1 periods=128 frames=135168 dropped=2048 silence=256 queued=0 rate=48000 bits=16 channels=2 DSP=0',
   'marble-audio-queue-test: complete PASS=1']
  if oracle is not None:
   expected=[expected[0],'marble-audio-signal-test: writes_and_RAM_copy PASS=1 frames=2048 rate=48000 bits=16 channels=2 pattern=triangle1k raw_peak=504 consumed=2048 dropped=0 queued=0 audible=0','marble-audio-signal-test: complete PASS=1']
   r['RAM_audio_queue_test_frames']=2048
   if a.owner_refill:
    expected=[expected[0],'marble-audio-refill-test: phase=observed-empty submitted=3 completed=3 feed=512 consumed=512 silence=256 queued=0 pending=0 audible=0','marble-audio-refill-test: phase=retired PASS=1 input_frames=1792 output_frames=2048 periods=8 consumed=1792 silence=256 queued=0 RAM_copy=1 HAL_sink=0 audible=0','marble-audio-refill-test: complete PASS=1']
    r['RAM_audio_queue_test_frames']=1792
   if a.owner_stream:
    expected=[expected[0],'marble-audio-stream-test: phase=observed-empty submitted=3 completed=3 feed=512 consumed=512 silence=256 queued=0 pending=0 audible=0','marble-audio-stream-test: phase=retired PASS=1 input_frames=5888 output_frames=6144 periods=24 consumed=5888 silence=256 queued=0 RAM_copy=1 flow_control=1 writes=19 HAL_sink=0 audible=0','marble-audio-stream-test: complete PASS=1']
    r['RAM_audio_queue_test_frames']=5888
  elif a.owner_package:
   # The old RAM test sleeps six seconds AFTER all queue assertions. The
   # fixed18 owner session must not be extended to await that unrelated dwell.
   expected=expected[:-1]
   r['queue_post_assertion_6s_dwell_completion_observed']='marble-audio-queue-test: complete PASS=1' in text
  for marker in expected:
   if text.count(marker)!=1:raise RuntimeError('missing/duplicate queue evidence: '+marker)
  if 'panic(cpu' in text or 'EARLY KERNEL EXCEPTION' in text or 'marble-thermal: STOP requested' in text:raise RuntimeError('kernel fault/temperature stop')
  for marker in ('marble-cpucap: restored PASS=1 active=0 dirty=0','marble-efi-idle: restored PASS=1 active=0 dirty=0'):
   if marker not in text:raise RuntimeError('resource restore not verified: '+marker)
  if a.native_transport:
   rows=[dict(re.findall(r'(\w+)=([^\s]+)',line)) for line in text.splitlines() if 'marble-audio-transport: ABI=' in line]
   if len(rows)!=1:raise RuntimeError('native SMEM evidence missing/duplicate')
   row=rows[0];expected=dict(ABI='MAUDIO-NATIVE-GLINK-RO-v1',PASS=1,partition_bytes=196608,RX_present=0,map_released=1,SMEM_writes=0,IPCC_writes=0,PAS_calls=0,DSP=0,hardware_audio=0)
   if any(row.get(k)!=str(v) for k,v in expected.items()):raise RuntimeError('native SMEM qualification failed')
   part=int(row['partition'],16);desc=int(row['descriptor'],16);fifo=int(row['TX_FIFO'],16)
   indices=[int(v,16) for v in row['indices'].split(',')]
   if not (0x1000<=part<=0x1ff000-196608 and part+32<=desc<=part+196608-32 and part+32<=fifo<=part+196608-16384 and (desc+32<=fifo or fifo+16384<=desc) and len(indices)==4 and indices[1:3]==[0,0] and all(0<=v<16384 and not v&7 for v in indices)):raise RuntimeError('native SMEM geometry invalid')
   r['native_SMEM_readonly_verified']=True
  if a.owner_package:
   import importlib.util
   spec=importlib.util.spec_from_file_location('xnu_owner_audit',ROOT/'tools/marble-audio-owner-report.py');audit=importlib.util.module_from_spec(spec);spec.loader.exec_module(audit)
   r['real_XNU_DSP_owner_audit']=audit.inspect(text,board=a.owner_board,periods=a.owner_periods,pcm=a.owner_pcm,signal_oracle=oracle,tpl_sampling=a.owner_tpl_sampling,usb_poll_lease=a.owner_usb_poll_lease,board_held=a.board_held,refill=a.owner_refill,qmp_close_snapshot=a.owner_qmp_close_snapshot,native_link_only=a.owner_native_link,stream_window=a.owner_stream,runtime_sampling=a.owner_runtime_sampling,usb_qmp_lease=a.owner_usb_qmp_lease,fast_handshake=a.owner_fast_handshake,handshake_ms=a.owner_handshake_ms,warmup_ms=a.owner_warmup_ms,prelaunch=a.owner_prelaunch,display_dma_only=a.owner_display_dma_only);save()
   if not r['real_XNU_DSP_owner_audit']['passed']:raise RuntimeError('real XNU owner audit: '+str(r['real_XNU_DSP_owner_audit']['errors']))
   r['real_XNU_DSP_owner_verified']=True
  r['CPU_and_EFI_restored']=True
  r['RAM_audio_queue_verified']=True;r['kernel_sha256']=hashlib.sha256((a.plan/'mach.marble-source').read_bytes()).hexdigest();save()
 except Exception as e:
  r['error']=str(e)
  console=a.out/'kernel-run.txt'
  if console.is_file():
   match=re.search(r'preserved log: ([^\r\n]+)',console.read_text(errors='replace'))
   if match:
    source=Path(match[1].strip()).resolve()
    if source.is_relative_to(ROOT/'out') and source.is_file():
     raw=source.read_bytes();(a.out/'returned.log').write_bytes(raw)
     text=raw.split(b'\0PMOC')[0].decode(errors='replace')
     r['pre_DSP_ADC38_refusal']=bool(re.search(r'sample=BATT_THERM .*physical=38\b',text) and
        'aud-owner-session: PASS=0 launched=0 protocol_completed=0 DMA_restored=0' in text and
        'amp_commands=0' in text and 'marble-pil-boot: auth ' not in text)
  save();print('Queue probe error:',e,flush=True)
 finally:
  if rebooted:
   devices=call([MARBLE_FASTBOOT,'devices'],'recovery-devices')
   if 'xnuloader' in devices:
    state=fb('getvar','result',label='recovery-state')
    if 'result: running' in state:raise RuntimeError('kernel still running; retain loader and hardware watchdog')
    fb('oem','log',label='recovery-log-stage')
    fb('get_staged',a.out/'recovery.log',label='recovery-log-fetch')
    fb('reboot',label='return-android')
   elif MARBLE_ANDROID_SERIAL in devices:
    call([MARBLE_FASTBOOT,'-s',MARBLE_ANDROID_SERIAL,'reboot'],'stock-return-android')
   call([MARBLE_ADB,'-s',MARBLE_ANDROID_SERIAL,'wait-for-device'],'wait-android',100)
   screen_off(a.out);_,after=battery_snapshot(a.out,'battery-after');r['battery_after_c']=after['temperature_c'];r['android_screen_off']=True
   r['boot_id_after']=call([MARBLE_ADB,'-s',MARBLE_ANDROID_SERIAL,'shell','cat','/proc/sys/kernel/random/boot_id'],'boot-id-after').strip()
   r['cold_reset_verified']=bool(r['boot_id_after'] and r['boot_id_after']!=r.get('boot_id_before'))
  r['passed']=bool((not a.owner_package or r.get('real_XNU_DSP_owner_verified')) and r.get('RAM_audio_queue_verified') and (not a.native_transport or r.get('native_SMEM_readonly_verified')) and not r.get('error') and r.get('cold_reset_verified') and r.get('android_screen_off') and r.get('battery_after_c',100)<38);save()
 if a.owner_package and rebooted:
  label=('STREAM24_' if a.owner_stream else '')+('REFILL_' if a.owner_refill else '')+('BOARD_HELD_' if a.board_held else '')+'XNU_OWNER_'+('PCM' if a.owner_pcm else 'ZERO')+str(a.owner_periods)+('_MUTED_BOARD' if a.owner_board else '')
  (a.boot_image.parent/'hardware-status.json').write_text(json.dumps(dict(image_sha256=hashlib.sha256(a.boot_image.read_bytes()).hexdigest(),hardware_status='PASS_ONE_'+label if r['passed'] else 'FAILED_'+label+'_DO_NOT_REBOOT',boot_allowed=r['passed'],evidence=str(a.out),audio_acceptance=False),indent=2)+'\n')
 print(json.dumps(r,indent=2),flush=True)
 if not r['passed']:raise SystemExit(1)
if __name__=='__main__':main()
