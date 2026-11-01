"""Read fixed Android hardware/configuration references; no mixer or register writes."""
from marble_local import ADB as MARBLE_ADB, ANDROID_SERIAL as MARBLE_ANDROID_SERIAL
import argparse,hashlib,json,subprocess
from pathlib import Path
from marble_android import battery_snapshot,screen_off

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
 a.out.mkdir(exist_ok=False);records={}
 commands={
  'cards':'cat /proc/asound/cards',
  'pcm':'cat /proc/asound/pcm',
  'sound-sysfs':'ls -l /sys/class/sound',
  'i2c-names':'for p in /sys/bus/i2c/devices/*/name; do echo "$p"; cat "$p"; done',
  'audio-properties':'getprop | grep -iE "audio|sound|sku|ro.board.platform|ro.hardware"',
  'audio-config-files':'find /vendor/etc /odm/etc -maxdepth 3 -type f -iname "*mixer*" -o -iname "*resource*" -o -iname "*aw882*" -o -iname "*audio*platform*"',
  'mixer-tool':'command -v tinymix; command -v agmplay; command -v capi_play',
  'device-tree-access':'ls -ld /proc/device-tree /sys/firmware/devicetree/base /sys/firmware/fdt',
 }
 for name,command in commands.items():
  r=subprocess.run([MARBLE_ADB,'-s',MARBLE_ANDROID_SERIAL,'shell',command],capture_output=True,timeout=15)
  data=r.stdout+r.stderr;dest=a.out/(name+'.txt');dest.write_bytes(data)
  records[name]=dict(command=command,exit_code=r.returncode,bytes=len(data),sha256=hashlib.sha256(data).hexdigest())
 _,battery=battery_snapshot(a.out,'battery-reference');screen_off(a.out)
 (a.out/'reference.json').write_text(json.dumps(dict(records=records,battery_c=battery.get('temperature_c'),
  scope='read-only Android ALSA/sysfs/properties/vendor filename reference; no mixer, I2C, GPIO, amplifier, calibration, playback or userdata access'),indent=2)+'\n')
 print('Saved hardware/configuration references; Android screen OFF; battery',battery.get('temperature_c'))
if __name__=='__main__':main()
