"""One serialized RAM-only real DSP/native-endpoint test, then Android OFF."""
from marble_local import ADB as MARBLE_ADB, ANDROID_SERIAL as MARBLE_ANDROID_SERIAL, FASTBOOT as MARBLE_FASTBOOT
import argparse,hashlib,importlib.util,json,re,subprocess,time
from pathlib import Path
from marble_android import battery_snapshot,screen_off,verify_battery_preflight
ROOT=Path(__file__).resolve().parents[1]

def main():
 p=argparse.ArgumentParser(description=__doc__)
 p.add_argument('--plan',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
 plan=a.plan.resolve();a.out=a.out.resolve();a.out.mkdir(exist_ok=False)
 image=plan/'loader-boot.img';sha=hashlib.sha256(image.read_bytes()).hexdigest()
 status=plan/'hardware-status.json'
 previous=json.loads(status.read_text()) if status.exists() else {}
 if previous.get('boot_allowed') is False:raise RuntimeError('candidate disabled by previous hardware failure')
 profile=json.loads((plan/'build-profile.json').read_text())
 if profile.get('audio_native_endpoint') is not True or profile.get('firmware_only') is not True or profile.get('audio_input_format')!='ios48-mfc':raise ValueError('wrong native provider profile')
 for name,digest in json.loads((plan/'compiled-inputs.json').read_text()).items():
  if hashlib.sha256((ROOT/name).read_bytes()).hexdigest()!=digest:raise ValueError('compiled source drift: '+name)
 spec=importlib.util.spec_from_file_location('native_audio_audit',ROOT/'tools/marble-audio-native-report.py')
 audit=importlib.util.module_from_spec(spec);spec.loader.exec_module(audit)
 report=dict(image_sha256=sha,audio_acceptance=False,XNU_sink=False,continuous_playback=False)
 rebooted=attempted=returned=False
 def save():(a.out/'exercise.json').write_text(json.dumps(report,indent=2)+'\n')
 def call(args,label,timeout=12,check=True):
  r=subprocess.run(list(map(str,args)),cwd=ROOT,capture_output=True,timeout=timeout)
  raw=r.stdout+r.stderr;(a.out/(label+'.txt')).write_bytes(raw)
  if check and r.returncode:raise RuntimeError(label+': '+raw.decode(errors='replace')[-1600:])
  return raw.decode(errors='replace')
 def fb(*args,label,timeout=12,check=True):return call([MARBLE_FASTBOOT,'-s','xnuloader',*args],label,timeout,check)
 def devices(label):return call([MARBLE_FASTBOOT,'devices'],label)
 try:
  _,before=battery_snapshot(a.out,'battery-before');verify_battery_preflight(a.out/'battery-before.json')
  report['battery_before_c']=before['temperature_c'];screen_off(a.out)
  boot_id=call([MARBLE_ADB,'-s',MARBLE_ANDROID_SERIAL,'shell','cat','/proc/sys/kernel/random/boot_id'],'boot-id-before').strip()
  if not re.fullmatch('[0-9a-fA-F-]{36}',boot_id):raise ValueError('invalid Android boot ID')
  report['boot_id_before']=boot_id;save()
  call([MARBLE_ADB,'-s',MARBLE_ANDROID_SERIAL,'reboot','bootloader'],'reboot-bootloader');rebooted=True
  deadline=time.monotonic()+45
  while MARBLE_ANDROID_SERIAL not in devices('stock-devices'):
   if time.monotonic()>deadline:raise RuntimeError('stock fastboot missing')
   time.sleep(1)
  call(['python','tools/marble-run.py','--serial',MARBLE_ANDROID_SERIAL,'boot','--image',image],'temporary-boot',70)
  deadline=time.monotonic()+45
  while 'xnuloader' not in devices('loader-devices'):
   if time.monotonic()>deadline:raise RuntimeError('loader missing')
   time.sleep(1)
  if 'result: idle' not in fb('getvar','result',label='loader-state'):raise RuntimeError('fresh idle loader required')
  if 'EFI-owner-XNU-endpoint-MFC-zero2-proof-v2' not in fb('getvar','marble-audio-native-endpoint',label='native-capability'):raise RuntimeError('native owner proof capability absent')
  if 'ram-only-no-block-io' not in fb('getvar','storage-policy',label='storage-policy'):raise RuntimeError('RAM storage policy absent')
  fb('oem','clearscreen',label='blank-display');fb('oem','idle-probe',label='idle-probe')
  fb('stage',ROOT/'out/adsp-owned-package-v1-plan/dependencies.bin',label='stage-dsp-package',timeout=40)
  attempted=True;report['DSP_command_attempted']=True;save()
  status.write_text(json.dumps(dict(image_sha256=sha,boot_allowed=False,hardware_status='IN_PROGRESS_DO_NOT_REBOOT',audio_acceptance=False),indent=2)+'\n')
  try:
   fb('oem','adsp-tdm-pcm-graph-cycle',label='native-cycle',timeout=40)
  finally:
   fb('oem','log',label='stage-log');fb('get_staged',a.out/'native.log',label='capture-log')
  text=(a.out/'native.log').read_text(errors='replace');report['native']=audit.inspect(text,require_proof=True);save()
  if not report['native']['passed']:raise RuntimeError('native hardware audit: '+str(report['native']['errors']))
  fb('reboot',label='cold-reset');returned=True
 except Exception as e:
  report['error']=str(e);save();print(str(e),flush=True)
 finally:
  if rebooted and not returned:
   if attempted:
    print('Retaining firmware watchdog; waiting for Android without another DSP call',flush=True)
   else:
    if 'xnuloader' in devices('recovery-devices'):fb('reboot',label='pre-DSP-reset')
    elif MARBLE_ANDROID_SERIAL in devices('recovery-devices-stock'):
     call([MARBLE_FASTBOOT,'-s',MARBLE_ANDROID_SERIAL,'reboot'],'stock-reset')
  if rebooted:
   deadline=time.monotonic()+105
   while time.monotonic()<deadline:
    ready=call([MARBLE_ADB,'-s',MARBLE_ANDROID_SERIAL,'shell','getprop','sys.boot_completed'],'android-ready',check=False)
    if ready.strip()=='1':break
    time.sleep(1)
   else:raise RuntimeError('Android recovery not observed')
   after_id=call([MARBLE_ADB,'-s',MARBLE_ANDROID_SERIAL,'shell','cat','/proc/sys/kernel/random/boot_id'],'boot-id-after').strip()
   report['cold_reset_verified']=after_id!=report.get('boot_id_before') and bool(re.fullmatch('[0-9a-fA-F-]{36}',after_id))
   _,after=battery_snapshot(a.out,'battery-after');report['battery_after_c']=after['temperature_c']
   screen_off(a.out);report['Android_screen_off_verified']=True;save()
  passed=report.get('native',{}).get('passed',False) and report.get('cold_reset_verified',False) and report.get('Android_screen_off_verified',False)
  if attempted:
   status.write_text(json.dumps(dict(image_sha256=sha,boot_allowed=bool(passed),hardware_status='PASS_ONE_NATIVE_ZERO2_PROVIDER' if passed else 'FAILED_DO_NOT_REBOOT_UNCHANGED',evidence=str(a.out.relative_to(ROOT)),audio_acceptance=False,XNU_sink=False),indent=2)+'\n')
  save()
 print(json.dumps(report,indent=2),flush=True)
 raise SystemExit(not report.get('native',{}).get('passed',False))

if __name__=='__main__':main()
