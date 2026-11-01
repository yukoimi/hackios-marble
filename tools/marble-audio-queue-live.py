"""One bounded black RAM-only audio-device probe, then Android screen off."""
from marble_local import ADB as MARBLE_ADB, ANDROID_SERIAL as MARBLE_ANDROID_SERIAL, FASTBOOT as MARBLE_FASTBOOT
import argparse,hashlib,json,re,subprocess,time
from pathlib import Path
from marble_android import battery_snapshot,screen_off
ROOT=Path(__file__).resolve().parents[1]
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--out',type=Path,required=True)
 p.add_argument('--native-transport',action='store_true',help='also require readonly native SMEM discovery; no DSP start')
 p.add_argument('--plan',type=Path,default=Path('out/audio-queue-v195-plan'))
 p.add_argument('--boot-image',type=Path,default=Path('out/marble-vsync-daily-v11-loader-boot.img'));a=p.parse_args()
 a.out.mkdir(exist_ok=False);r=dict(scope='native readonly SMEM discovery plus synthetic kernel queue' if a.native_transport else 'synthetic kernel device/queue probe; no iOS HAL or DSP acceptance',audio_acceptance=False)
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
  if not 0<=before['temperature_c']<38:raise RuntimeError('battery preflight')
  screen_off(a.out);r['android_screen_off_before']=True
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
  r['CPU_and_EFI_restored']=True
  r['RAM_audio_queue_verified']=True;r['kernel_sha256']=hashlib.sha256((a.plan/'mach.marble-source').read_bytes()).hexdigest();save()
 except Exception as e:r['error']=str(e);save();print('Queue probe error:',e,flush=True)
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
  r['passed']=bool(r.get('RAM_audio_queue_verified') and (not a.native_transport or r.get('native_SMEM_readonly_verified')) and not r.get('error') and r.get('cold_reset_verified') and r.get('android_screen_off') and r.get('battery_after_c',100)<38);save()
 print(json.dumps(r,indent=2),flush=True)
 if not r['passed']:raise SystemExit(1)
if __name__=='__main__':main()
