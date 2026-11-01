"""Unattended RAM-only input/framebuffer regression against reviewed captures."""
import argparse,json,subprocess,time,shutil
from pathlib import Path
from PIL import Image,ImageChops,ImageStat,ImageDraw

def scroll_difference(left,right):
    """Register a bounded inertial stop offset in the scrolling list only.
    Header/footer are excluded from registration, never warped/rescaled. The
    original full-frame error remains in the report for independent review.
    """
    x=Image.open(left).convert('RGB');y=Image.open(right).convert('RGB')
    if x.size!=y.size or x.size!=(1080,2400):return float('inf'),0
    box=(40,480,1040,2080);fixed=y.crop(box);best=(float('inf'),0)
    for shift in range(-160,161,4):
        moved=x.crop((40,480+shift,1040,2080+shift))
        delta=sum(ImageStat.Stat(ImageChops.difference(moved,fixed)).mean)/3
        if delta<best[0]:best=(delta,shift)
    return best
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--out',required=True,type=Path)
    p.add_argument('--reference',default=Path('out/ui-gmu-hfi'),type=Path)
    p.add_argument('--log-snapshots',action='store_true',help='preserve live GPU logs between each input/capture')
    p.add_argument('--warmup',default=30,type=int);a=p.parse_args();a.out.mkdir(parents=True,exist_ok=True)
    def snapshot():
        if a.log_snapshots:
            target=subprocess.check_output(['python','tools/marble-run.py','snapshot'],text=True).strip()
            logdir=a.out/'logs';logdir.mkdir(exist_ok=True)
            shutil.copyfile(target,logdir/(str(len(list(logdir.glob('*.log')))).zfill(3)+'.log'))
    def action(kind,name,*coords,settle=3):
        subprocess.run(['python','tools/marble-ui-regression.py',kind,name,*map(str,coords),'--out',str(a.out),'--settle',str(settle)],check=True)
        snapshot()
    def difference(left,right):
        x=Image.open(left).convert('RGB');y=Image.open(right).convert('RGB')
        if x.size!=y.size:raise RuntimeError('reference size differs')
        return sum(ImageStat.Stat(ImageChops.difference(x.crop((0,130,x.width,x.height)),y.crop((0,130,y.width,y.height)))).mean)/3
    # A retained old home framebuffer can precede the new userspace/input
    # bridge. A visually matching first frame alone cannot establish readiness.
    snapshot()
    for seconds in range(0,max(0,min(a.warmup,120)),5):
        print(json.dumps(dict(warmup_seconds=seconds)),flush=True);time.sleep(5)
    ready=False
    for i in range(18):
        action('capture','ready',settle=5)
        delta=difference(a.out/'ready.png',a.reference/'ready2.png')
        print(json.dumps(dict(ready_attempt=i,reference_delta=delta)),flush=True)
        if delta<12:ready=True;break
    if not ready:raise RuntimeError('reviewed home layout not reached; no input sent')
    actions=[('tap','settings',(663,1700)),('swipe','scroll',(530,1900,530,700)),
        ('swipe','home',(540,2375,540,1000)),('hold','menu',(663,1700)),('tap','dismiss',(1010,1700))]
    results=[]
    for kind,name,coords in actions:
        action(kind,name,*coords)
        delta=difference(a.out/(name+'.png'),a.reference/(name+'.png'))
        registered,shift=scroll_difference(a.out/(name+'.png'),a.reference/(name+'.png')) if name=='scroll' else (delta,0)
        for attempt in range(8):
            if registered<12:break
            action('capture',name,settle=3)
            delta=difference(a.out/(name+'.png'),a.reference/(name+'.png'))
            registered,shift=scroll_difference(a.out/(name+'.png'),a.reference/(name+'.png')) if name=='scroll' else (delta,0)
        record=dict(name=name,reference_delta=delta,passed=delta<12)
        if name=='scroll':
            record.update(registered_delta=registered,vertical_shift=shift,passed=registered<12)
        results.append(record)
    sheet=Image.new('RGB',(270*5,625),'#333333');draw=ImageDraw.Draw(sheet)
    for i,(_,name,_) in enumerate(actions):
        im=Image.open(a.out/(name+'.png')).convert('RGB');im.thumbnail((270,600));sheet.paste(im,(270*i,25));draw.text((270*i+5,5),name,fill='white')
    sheet.save(a.out/'contact-sheet.jpg')
    passed=all(v['passed'] for v in results)
    report=dict(passed=passed,ready=True,checks=results,input_scope='synthetic mailbox -> bridge -> HID -> UI',display_scope='framebuffer images, no optical observation',temperature_observed=False)
    (a.out/'report.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf8');print(json.dumps(report),flush=True)
    raise SystemExit(0 if passed else 1)
if __name__=='__main__':main()
