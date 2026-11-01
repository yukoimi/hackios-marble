"""Return-to-Android screen-off confirmation, without changing saved settings."""
from marble_local import ADB as MARBLE_ADB, ANDROID_SERIAL as MARBLE_ANDROID_SERIAL
import json, re, subprocess, time
from pathlib import Path


def battery_snapshot(directory, label):
    result=subprocess.run([MARBLE_ADB,'-s',MARBLE_ANDROID_SERIAL,'shell','dumpsys','battery'],
        capture_output=True,text=True,errors='replace',timeout=8)
    raw=result.stdout+result.stderr
    (directory/(label+'.txt')).write_text(raw,encoding='utf8')
    temperature=re.search(r'^\s*temperature:\s*(-?\d+)\s*$',result.stdout,re.M)
    confirmed=result.returncode==0 and temperature is not None
    value=int(temperature[1]) if temperature else None
    record=dict(confirmed=confirmed,serial=MARBLE_ANDROID_SERIAL,at_unix=time.time(),
        temperature_decic=value,temperature_c=value/10 if value is not None else None,
        scope='single Android BatteryService reading; not an in-iOS peak measurement')
    path=directory/(label+'.json')
    path.write_text(json.dumps(record,indent=2)+'\n',encoding='utf8')
    if not confirmed:raise RuntimeError('Android battery temperature was not confirmed')
    return path,record


def verify_battery_preflight(path, now=None):
    record=json.loads(Path(path).read_text())
    age=(time.time() if now is None else now)-record.get('at_unix',0)
    temperature=record.get('temperature_decic')
    if (record.get('confirmed') is not True or record.get('serial')!=MARBLE_ANDROID_SERIAL or
        not 0<=age<=180 or type(temperature) is not int or not 0<=temperature<380):
        raise ValueError('short test needs a fresh Android battery reading below 38 C, with headroom to the user 40 C target')
    return record


def screen_off(directory, seconds=90):
    deadline=time.monotonic()+seconds
    def adb(*args):
        return subprocess.run([MARBLE_ADB,'-s',MARBLE_ANDROID_SERIAL,'shell',*args],
                              capture_output=True,text=True,errors='replace',timeout=8)
    last='Android not ready'
    while time.monotonic()<deadline:
        ready=adb('getprop','sys.boot_completed')
        if ready.returncode==0 and ready.stdout.strip()=='1':
            off=adb('input','keyevent','223')
            if off.returncode==0:
                display=adb('dumpsys','display');last=display.stdout+display.stderr
                if display.returncode==0 and all(re.search(r'\b'+key+r'=OFF\b',last)
                                                for key in ('mScreenState','mActualState')):
                    (directory/'android-display-off.txt').write_text(last,encoding='utf8')
                    (directory/'android-screen-off.json').write_text(json.dumps(dict(
                        confirmed=True,screen_state='OFF',actual_state='OFF',
                        method='KEYCODE_SLEEP; no persistent settings change'),indent=2)+'\n')
                    return
            else:last=off.stdout+off.stderr
        time.sleep(1)
    (directory/'android-screen-off.json').write_text(json.dumps(dict(
        confirmed=False,last_error=last[-2000:]),indent=2)+'\n')
    raise RuntimeError('Android screen OFF was not confirmed within the bounded wait')
