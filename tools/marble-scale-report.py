"""Summarize GLES scale evidence and predeclared native gesture windows.

This does not infer optical refresh or smoothness from a counter alone.
"""
import argparse
import importlib.util
import json
from pathlib import Path
import re
import struct

ROOT=Path(__file__).resolve().parents[1]


def cadence_module():
    spec=importlib.util.spec_from_file_location('scale_cadence',ROOT/'tools/marble-frame-cadence.py')
    m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);return m


def inspect(directory):
    evidence=json.loads((directory/'exercise.json').read_text())
    lines=set()
    for path in [directory/'returned.log',*sorted((directory/'logs').glob('*.log'))]:
        for line in path.read_text(errors='replace').splitlines():
            if 'marble-gpu-scale:' in line or 'rwtouch: mark=' in line:lines.add(line)
    text='\n'.join(sorted(lines))
    checks=re.findall(r'selftest=(PASS|FAIL) cases=(\d+) bad_channels=(\d+)',text)
    stats=[]
    for m in re.finditer(r'marble-gpu-scale: seq=(\d+) swap=(\d+) offers=(\d+) finished=(\d+) failures=(\d+) accepted=(\d+) disabled=(\d+)([^\n]*)',text):
        row=dict(zip(('sequence','swap','offers','finished','failures','accepted','disabled'),map(int,m.groups()[:7])))
        row.update({k:int(v) for k,v in re.findall(r'(queue_us|work_us|total_us|timeouts)=(\d+)',m[8])});stats.append(row)
    last=max(stats,key=lambda r:r['sequence']) if stats else None
    helper=[dict(sequence=int(seq),scale_us=int(us),mean_us=int(mean)) for seq,us,mean in
        re.findall(r'marble-gpu-scale: seq=(\d+) frames=\d+ scale_us=(\d+) mean_us=(\d+)',text)]
    marks={m:int(t)/1e6 for m,t in re.findall(r'rwtouch: mark=([\w-]+) at_us=(\d+)',text)}
    cadence=cadence_module();recording=directory/'recording.bin';times=cadence.timestamps(recording)
    windows={}
    for phase in ('open-0','exit-0','open-1','exit-1'):
        if phase+'-begin' not in marks or phase+'-end' not in marks:continue
        begin,end=marks[phase+'-begin'],marks[phase+'-end']
        # The same fixed first two seconds for every phase/configuration.
        # These include startup pauses/settled frames, not handpicked peaks.
        try:windows[phase]=cadence.measure(times,begin,min(begin+2,end),60,cadence.recording_scope(recording))
        except ValueError as exc:windows[phase]={'error':str(exc)}
    # Count exact central image changes within those SAME windows so that
    # repeated/settled frames cannot masquerade as smooth animation. Status
    # clock and bottom home bar are excluded. This is a diagnostic proxy,
    # not a segmentation claim about which pixels should be animating.
    import numpy as np
    with recording.open('rb') as stream:
        magic,w,h,count,oldest,began=struct.unpack('<8sIIIIQ',stream.read(32))
        stride=8+w*h*3
        previous={};changes={name:[0,0] for name in windows}
        for index,t in enumerate(times):
            phase_names=[name for name in windows if name+'-begin' in marks and
                marks[name+'-begin']<=t<=min(marks[name+'-begin']+2,marks[name+'-end'])]
            if not phase_names:continue
            slot=(oldest+index)%count;stream.seek(32+slot*stride+8)
            rgb=np.frombuffer(stream.read(w*h*3),dtype=np.uint8).reshape(h,w,3)
            crop=rgb[int(.06*h):int(.94*h),int(.04*w):int(.96*w)]
            for name in phase_names:
                if name in previous:
                    difference=np.abs(crop.astype(np.int16)-previous[name].astype(np.int16))
                    changes[name][0]+=int(np.any(difference))
                    changes[name][1]+=int(float(difference.mean())>1.0)
                previous[name]=crop.copy()
        for name,(exact,visible) in changes.items():
            windows[name].update(central_exact_changes=exact,central_mean_delta_over_1=visible,
                change_scope='sampled RAM RGB central 92% width / 88% height; mean-channel delta threshold 1; not proof every retained frame contains useful animation')
    return dict(directory=str(directory),selftest_records=sorted(set(checks)),
        worker_registered='ready=1 geometry=' in text,last_reported_kernel_stats=last,
        count_scope='last periodic report is a lower bound, not the final frame count',
        last_reported_helper_stats=max(helper,key=lambda r:r['sequence']) if helper else None,
        late_results_discarded=len(set(re.findall(r'late_seq=(\d+) discarded=1',text))),
        native_sequence_complete=evidence.get('native_sequence_complete',False),
        gesture_windows=windows,window_rule='first 2.0 seconds after each native begin mark, clipped at end mark; unchanged for all candidates',
        window_limits='includes app-startup/settled time; cadence is RAM composition, not a proof of continuous animation or optical scanout',
        display_pacing=evidence.get('display_pacing_acceptance'),display_runtime=evidence.get('display_runtime'),
        battery_before_c=evidence.get('battery_before_c'),battery_after_c=evidence.get('battery_after_c'),
        battery_limit='Android endpoint readings only; no in-iOS battery peak telemetry',
        die_peak_c=evidence.get('observed_peak_mc',0)/1000,restoration=evidence.get('restoration'),
        render_scale=evidence.get('render_scale',3),
        android_screen_off=evidence.get('android_screen_off'),stable_60_fps_proven=False,visual_acceptance=None)


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('directories',type=Path,nargs='+');p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();results=[inspect(d) for d in a.directories]
    a.output.write_text(json.dumps(results,indent=2)+'\n',encoding='utf8')
    for row in results:
        print(row['directory'], 'scale=',row['last_reported_kernel_stats'])
        print('windows:',{k:round(v['observed_fps'],2) if 'observed_fps' in v else v for k,v in row['gesture_windows'].items()})


if __name__=='__main__':main()
