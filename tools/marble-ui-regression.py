#!/usr/bin/env python3
"""Bounded RAM-mailbox input and framebuffer captures; never physical sensor proof."""
from marble_local import FASTBOOT as MARBLE_FASTBOOT
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import time
from PIL import Image, ImageChops, ImageStat

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('action', choices=['capture', 'tap', 'swipe', 'hold','key','wake-unlock'])
    p.add_argument('name')
    p.add_argument('coords', type=int, nargs='*', help='pixel coordinates in framebuffer')
    p.add_argument('--out', type=Path, default=Path('out/ui-gmu-clocks'))
    p.add_argument('--settle', type=float, default=2)
    p.add_argument('--duration-ms',type=int,choices=range(100,1501),default=350,help='device-clock swipe duration; other actions retain their original timings')
    a = p.parse_args()
    device_clock = False
    wake_ready = None
    def fb(*args):
        r = subprocess.run([MARBLE_FASTBOOT,'-s','xnuloader',*map(str,args)],
                           capture_output=True, timeout=30)
        if r.returncode: raise RuntimeError((r.stdout+r.stderr).decode(errors='replace'))
        return (r.stdout+r.stderr).decode(errors='replace')
    def get(key):
        for line in fb('getvar',key).splitlines():
            if line.startswith(key+':'): return line.split(':',1)[1].strip()
        raise RuntimeError('missing '+key)
    for key,value in [('product','xnuloader'),('platform','marble'),
                      ('storage-policy','ram-only-no-block-io'),('marble-touch-test','mailbox-v1')]:
        if get(key)!=value: raise RuntimeError('identity/capability differs: '+key)
    if not get('result').startswith('running'): raise RuntimeError('kernel not running')
    if a.action in ('tap','swipe','wake-unlock'):
        try:device_clock=get('marble-touch-gesture')=='device-clock-v1'
        except RuntimeError:device_clock=False
    w,h,stride=map(int,get('screen').split())
    try:snapshot_backend=get('marble-snapshot')
    except RuntimeError:snapshot_backend='unadvertised'
    def event(kind,*coords):
        fb('oem','touch-test',kind,*coords)
    def point(x,y):
        if not 0<=x<w or not 0<=y<h: raise ValueError('coordinate outside framebuffer')
        return round(x*65535/(w-1)),round(y*65535/(h-1))
    n = dict(capture=0,tap=2,hold=2,swipe=4,key=1,**{'wake-unlock':0})[a.action]
    if len(a.coords)!=n: raise ValueError('wrong number of coordinates')
    a.out.mkdir(parents=True,exist_ok=True)
    if a.action in ('key','wake-unlock'):
        if a.action=='wake-unlock' and not device_clock:raise RuntimeError('wake-unlock requires device-clock gestures')
        key=a.coords[0] if a.action=='key' else 1
        if key not in (1,2,3):raise ValueError('key must be lock=1, volume up=2 or volume down=3')
        if get('marble-buttons')!='observer-v7-mailbox-v2':raise RuntimeError('button firmware ABI mismatch')
        down=False
        try:
            fb('oem','button-test','down',key);down=True;time.sleep(.08)
        finally:
            if down:fb('oem','button-test','up',key)
        if a.action=='wake-unlock':
            # selector12 on precedes the first completed lock-screen frame by
            # several seconds here. A blind one-second swipe lands before the
            # lock-screen UI is interactive; eight seconds can let it sleep
            # again. The presenter retains black pixels until that first swap,
            # so wait for actual nonblack scanout, then send exactly one swipe.
            began=time.monotonic();deadline=began+10;samples=0
            ready_raw=a.out/(a.name+'-wake-ready.bgra')
            while True:
                fb('oem','screenshot');fb('get_staged',ready_raw)
                pixels=ready_raw.read_bytes()
                if len(pixels)!=stride*h:raise RuntimeError('wake framebuffer length differs')
                ready_image=Image.frombytes('RGBA',(w,h),pixels,'raw','BGRA',stride).convert('RGB')
                samples+=1
                visible=bool(ready_image.getbbox())
                wake_ready=dict(samples=samples,seconds_to_visible=round(time.monotonic()-began,3),
                                visible=visible,source='first nonblack scanout after power-on; no fixed-delay guess')
                (a.out/(a.name+'-wake-progress.json')).write_text(json.dumps(wake_ready,indent=2)+'\n')
                if visible:break
                if time.monotonic()>=deadline:raise RuntimeError('no completed visible wake frame before deadline; no unlock swipe sent')
                time.sleep(.05)
            ready_image.save(a.out/(a.name+'-wake-ready.png'))
            fb('oem','touch-gesture',*point(w//2,h-25),*point(w//2,h//3),350)
            time.sleep(.39)
    elif device_clock:
        start=point(*a.coords[:2]);end=point(*a.coords[2:]) if a.action=='swipe' else start
        duration=a.duration_ms if a.action=='swipe' else 60
        fb('oem','touch-gesture',*start,*end,duration)
        time.sleep((duration+40)/1000)
    elif a.action!='capture':
        x,y=point(*a.coords[:2]); down=False
        try:
            event('down',x,y); down=True
            if a.action=='swipe':
                end=point(*a.coords[2:])
                for i in range(1,17):
                    time.sleep(.015)
                    event('move',round(x+(end[0]-x)*i/16),round(y+(end[1]-y)*i/16))
            else: time.sleep(.08 if a.action=='tap' else 1.2)
        finally:
            if down: event('up')
    time.sleep(max(0,min(a.settle,10)))
    fb('oem','screenshot'); raw=a.out/(a.name+'.bgra')
    fb('get_staged',raw)
    data=raw.read_bytes()
    if len(data)!=stride*h: raise RuntimeError('framebuffer length differs')
    im=Image.frombytes('RGBA',(w,h),data,'raw','BGRA',stride)
    im.save(a.out/(a.name+'.png'))
    info=dict(action=a.action,coords=a.coords,width=w,height=h,
              device_clock_gesture=device_clock,
              snapshot_backend=snapshot_backend,
              sha256=hashlib.sha256(data).hexdigest(),
              input_scope='synthetic mailbox -> bridge -> HID -> UI',
              display_scope=('completed composition under kernel mutex; no optical scanout observation'
                             if snapshot_backend=='completed-composition-mutex-v3-own-ram' else
                             'framebuffer capture without advertised consistency guarantee; no optical scanout observation'))
    if wake_ready is not None:info['wake_ready']=wake_ready
    (a.out/(a.name+'.json')).write_text(json.dumps(info,indent=2)+'\n',encoding='utf8')
    print(json.dumps(info))

if __name__=='__main__': main()
