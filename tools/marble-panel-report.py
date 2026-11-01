"""Compare fixed native panel gesture windows using RAM composition evidence.

Changes/latency are image-difference proxies. They do not prove optical FPS,
continuous useful animation, successful gestures, or physical touch behavior.
"""
import argparse,importlib.util,json,re
from pathlib import Path
import numpy as np
ROOT=Path(__file__).resolve().parents[1]
def report(path):
    spec=importlib.util.spec_from_file_location('panel_recording',ROOT/'tools/marble-recording-review.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
    frames=m.read_frames(path/'recording.bin');lines=set()
    for f in [path/'returned.log',*sorted((path/'logs').glob('*.log'))]:
        for line in f.read_text(encoding='utf8',errors='replace').splitlines():
            if 'rwtouch: mark=' in line or 'mediaserverd' in line:lines.add(line)
    text='\n'.join(lines);marks={name:int(at)/1e6 for name,at in re.findall(r'rwtouch: mark=([\w-]+) at_us=(\d+)',text)}
    arrays=[]
    for t,im in frames:
        a=np.asarray(im,dtype=np.int16);h,w=a.shape[:2]
        arrays.append((t,a[int(h*.10):int(h*.90),int(w*.05):int(w*.95)]))
    windows={}
    for name,begin in sorted(marks.items()):
        if not re.fullmatch(r'(control|notification)(-close)?-\d+-begin',name):continue
        end=min(begin+2.0,marks.get(name[:-5]+'end',begin+2.0));before=[(t,a) for t,a in arrays if t<=begin]
        selected=[(t,a) for t,a in arrays if begin<t<=end]
        if not before or not selected:
            windows[name]={'covered':False,'reason':'retained ring does not cover baseline/window'};continue
        last=before[-1][1];first=None;changes=0;exact=0;gaps=[];last_t=before[-1][0]
        for t,a in selected:
            delta=np.abs(a-last);changed=float(delta.mean())>1
            exact+=int(np.any(delta));changes+=int(changed)
            if changed and first is None:first=t-begin
            gaps.append(t-last_t);last=a;last_t=t
        windows[name]=dict(covered=True,begin=begin,end=end,frames=len(selected),central_mean_delta_over_1=changes,
            central_exact_changes=exact,first_mean_delta_over_1_ms=first*1000 if first is not None else None,
            max_composition_gap_ms=max(gaps)*1000,baseline_age_ms=(begin-before[-1][0])*1000)
    evidence=json.loads((path/'exercise.json').read_text())
    exits=sorted(line for line in lines if 'com.apple.mediaserverd [' in line and 'exited due to' in line)
    return dict(directory=str(path),window_rule='first 2 seconds after each marked native gesture; central 90% width / 80% height',
        scope=__doc__,windows=windows,media_exits=exits,media_exit_count=len(exits),
        media_compat_observed='redwood: mediaserverd:' in text,visual_acceptance=None,stable_60fps_proven=False,
        battery_before_c=evidence.get('battery_before_c'),battery_after_c=evidence.get('battery_after_c'),
        restoration=evidence.get('restoration'),android_screen_off=evidence.get('android_screen_off'))
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('directories',type=Path,nargs='+');p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    rows=[report(path) for path in a.directories];a.output.write_text(json.dumps(rows,indent=2)+'\n')
    for row in rows:print(row['directory'],'media_exits=',row['media_exit_count'],{k:round(v['first_mean_delta_over_1_ms'],1) if v.get('first_mean_delta_over_1_ms') is not None else None for k,v in row['windows'].items()})
if __name__=='__main__':main()
