"""Bounded unattended compositor exercise; synthetic input and framebuffer only."""
from marble_local import FASTBOOT as MARBLE_FASTBOOT
import argparse,json,subprocess,time,shutil
from pathlib import Path
from PIL import Image,ImageChops,ImageStat,ImageDraw

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--kernel',required=True,type=Path)
    p.add_argument('--ramdisk',required=True,type=Path)
    p.add_argument('--out',required=True,type=Path)
    p.add_argument('--software',action='store_true')
    p.add_argument('--rounds',type=int,default=2)
    a=p.parse_args()
    if not 1<=a.rounds<=4:p.error('rounds must be 1..4')
    a.out.mkdir(parents=True,exist_ok=False)
    logs=a.out/'logs';logs.mkdir();n=0
    def run(*args):
        return subprocess.check_output(list(map(str,args)),text=True,stderr=subprocess.STDOUT)
    def snapshot():
        nonlocal n
        source=run('python','tools/marble-run.py','snapshot').strip()
        shutil.copyfile(source,logs/f'{n:03}.log');n+=1
    def action(kind,name,*coords,settle=1):
        print(run('python','tools/marble-ui-regression.py',kind,name,*coords,'--out',a.out,'--settle',settle).strip(),flush=True)
        snapshot()
    frames=[];began=time.monotonic();returned=False
    print(run('python','tools/marble-run.py','run','--kernel',a.kernel,'--ramdisk',a.ramdisk,
        '--cpus','4','--seconds','240','--fb-threads','2','--input-mailbox','--gpu-probe','17',
        '--gpu-runtime','--detach',*[] if a.software else ['--gpu-composition']),flush=True)
    try:
        # Preserve bring-up before the 1 MB live log ring can wrap.
        for i in range(11):
            snapshot();time.sleep(2)
        reference=Image.open('out/ui-gmu-hfi/ready2.png').convert('RGB')
        for i in range(12):
            action('capture',f'ready-{i:02}',settle=2)
            im=Image.open(a.out/f'ready-{i:02}.png').convert('RGB')
            delta=sum(ImageStat.Stat(ImageChops.difference(im.crop((0,130,1080,2400)),reference.crop((0,130,1080,2400)))).mean)/3
            print(json.dumps(dict(ready=i,reference_delta=delta)),flush=True)
            if delta<12:break
        else:raise RuntimeError('home layout not reached; no input sent')
        actions=[('swipe','today',(180,1200,930,1200)),('swipe','today-home',(930,1200,180,1200)),
          ('swipe','page2',(930,1200,180,1200)),('swipe','library',(930,1200,180,1200)),
          ('swipe','library-back',(180,1200,930,1200)),('swipe','home',(180,1200,930,1200)),
          ('hold','menu',(663,1700)),('tap','dismiss',(1010,1700))]
        for iteration in range(a.rounds):
            for kind,name,coords in actions:
                key=f'{iteration:02}-{name}';action(kind,key,*coords,settle=2)
                frames.append(key)
        sheet=Image.new('RGB',(8*180,a.rounds*425),'#333333');draw=ImageDraw.Draw(sheet)
        for i,key in enumerate(frames):
            im=Image.open(a.out/(key+'.png')).convert('RGB');im.thumbnail((180,400))
            x=(i%8)*180;y=(i//8)*425;sheet.paste(im,(x,y+25));draw.text((x+3,y+5),key,fill='white')
        sheet.save(a.out/'contact-sheet.jpg')
    finally:
        # Request audited drain; never reboot over an active DMA lease.
        print(run(MARBLE_FASTBOOT,'-s','xnuloader','oem','stop'),flush=True)
        for i in range(30):
            status=run(MARBLE_FASTBOOT,'-s','xnuloader','getvar','result')
            if 'result: returned ' in status:
                run(MARBLE_FASTBOOT,'-s','xnuloader','get_staged',a.out/'returned.log');returned=True;break
            time.sleep(1)
        (a.out/'exercise.json').write_text(json.dumps(dict(returned=returned,frames=frames,
            wall_seconds=time.monotonic()-began,software=a.software,
            input_scope='synthetic mailbox -> bridge -> HID -> UI',
            display_scope='device framebuffer; no optical measurement',
            visual_pass=None,temperature_observed=False),indent=2)+'\n')
        if not returned:raise RuntimeError('audited return not confirmed')

if __name__=='__main__':main()
