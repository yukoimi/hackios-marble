"""Inspect every retained source/composition frame; candidates require visual review.

This records the display source, not optical scanout or a physical touch test.
Green/black pixel counts only rank frames; they are never an automatic pass.
"""
import argparse, hashlib, json, re, struct
from pathlib import Path
import numpy as np
from PIL import Image, ImageDraw


def read_frames(path):
    data=path.read_bytes()
    magic,w,h,n,oldest,began=struct.unpack_from('<8sIIIIQ',data)
    if magic not in (b'RWREC003',b'RWREC004') or not 1<=w<=2048 or not 1<=h<=4096 or not n or oldest>=n:
        raise ValueError('invalid recording header')
    size=w*h*3;stride=size+8
    if len(data)!=32+n*stride:raise ValueError('truncated or extra frame bytes')
    frames=[]
    for i in list(range(oldest,n))+list(range(oldest)):
        at=32+i*stride;t=struct.unpack_from('<Q',data,at)[0]
        frames.append(((began+t)/1e9,Image.frombytes('RGB',(w,h),data[at+8:at+stride])))
    if any(a[0]>=b[0] for a,b in zip(frames,frames[1:])):raise ValueError('nonmonotonic ring')
    return frames


def sheet(frames,indices,path,columns=8):
    w,h=frames[0][1].size
    out=Image.new('RGB',(columns*w,((len(indices)+columns-1)//columns)*(h+24)),'#333333')
    draw=ImageDraw.Draw(out)
    for j,i in enumerate(indices):
        x=j%columns*w;y=j//columns*(h+24)
        draw.text((x+2,y+3),f'{i} {frames[i][0]:.3f}s',fill='white')
        out.paste(frames[i][1],(x,y+24))
    out.save(path)


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('directory',type=Path)
    p.add_argument('--sheet-all',action='store_true',help='include every retained frame in numbered 64-frame sheets')
    a=p.parse_args();frames=read_frames(a.directory/'recording.bin')
    records=[]
    for i,(t,im) in enumerate(frames):
        rgb=np.asarray(im).astype(np.int16);r,g,b=rgb[:,:,0],rgb[:,:,1],rgb[:,:,2]
        # Settings icon region, excluding the green Phone icon in the Dock.
        y0,y1=int(im.height*.62),int(im.height*.78)
        x0,x1=int(im.width*.50),int(im.width*.73)
        green=((g>180)&(r<80)&(b<100))[y0:y1,x0:x1]
        black=(rgb.max(axis=2)<8)
        records.append(dict(index=i,seconds=t,settings_green=int(green.sum()),black=int(black.sum())))
    # Sparse overview plus complete neighborhoods around the largest suspects.
    sheet(frames,list(range(0,len(frames),5)),a.directory/'recording-overview.jpg')
    ranked=sorted(records,key=lambda v:v['settings_green'],reverse=True)
    selected=set()
    for row in ranked[:8]:selected.update(range(max(0,row['index']-2),min(len(frames),row['index']+3)))
    sheet(frames,sorted(selected),a.directory/'recording-green-candidates.png')
    black_ranked=sorted(records,key=lambda v:v['black'],reverse=True)
    selected=set()
    for row in black_ranked[:8]:selected.update(range(max(0,row['index']-2),min(len(frames),row['index']+3)))
    sheet(frames,sorted(selected),a.directory/'recording-black-candidates.png')
    if a.sheet_all:
        for start in range(0,len(frames),64):
            sheet(frames,list(range(start,min(start+64,len(frames)))),a.directory/f'frames-{start:03}.jpg')
    durations=[max(10,min(1000,round((frames[i+1][0]-t)*1000))) if i+1<len(frames) else 300
               for i,(t,_) in enumerate(frames)]
    frames[0][1].save(a.directory/'recording.gif',save_all=True,append_images=[im for _,im in frames[1:]],
                      duration=durations,loop=0)
    log=a.directory/'complete.log'
    if not log.exists():log=a.directory/'returned.log'
    text=log.read_text(encoding='utf8',errors='replace')
    marks=[dict(name=m,seconds=int(t)/1e6) for m,t in re.findall(r'rwtouch: mark=([\w-]+) at_us=(\d+)',text)]
    with (a.directory/'recording.bin').open('rb') as f:magic=f.read(8)
    source=('reduced completed multi-layer composition under kernel mutex; not optical scanout'
            if magic==b'RWREC004' else 'reduced source IOSurface after panel-memory copy; not optical scanout')
    report=dict(source=source,
                sha256=hashlib.sha256((a.directory/'recording.bin').read_bytes()).hexdigest(),
                frames=len(frames),start=frames[0][0],end=frames[-1][0],marks=marks,
                green_candidates=ranked[:12],black_candidates=black_ranked[:12],
                frame_metrics=records,visual_pass=None)
    (a.directory/'recording-review.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({k:v for k,v in report.items() if k!='frame_metrics'},indent=2))


if __name__=='__main__':main()
