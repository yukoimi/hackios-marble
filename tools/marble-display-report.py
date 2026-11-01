"""Audit accelerated display experiments; visual and optical acceptance stay separate."""
import argparse, importlib.util, json, re
from pathlib import Path


def module(name, filename):
    spec=importlib.util.spec_from_file_location(name,Path(__file__).with_name(filename))
    mod=importlib.util.module_from_spec(spec);spec.loader.exec_module(mod)
    return mod


composition=module('composition','marble-gpu-composition-report.py')
recording=module('recording','marble-recording-review.py')
context=module('gpucontext','marble-gpu-context-report.py')


def inspect(directory):
    path=directory/'complete.log';raw=path.read_bytes();text=raw.decode('utf8')
    report=composition.blit.inspect(path,live='service');errors=report['errors']
    proof=json.loads(path.with_suffix('.provenance.json').read_text());pieces=[]
    for row in proof['records']:
        source=composition.evidence_path(row['path'],directory).read_bytes()
        if composition.sha(source)!=row['sha256']:errors.append('raw log source hash differs')
        overlap=row['overlap_bytes'];at=row['copy_offset']
        if overlap and (overlap<1024 or source[at-overlap:at]!=b''.join(pieces)[-overlap:]):
            errors.append('raw log continuity differs')
        pieces.append(source[at:at+row['copy_bytes']])
    final=composition.evidence_path(proof['header_source'],directory).read_bytes()
    marker=b'== KERNEL LOG ==\n';end=b'\n== END OF LOG ==\n'
    reconstructed=final[:final.index(marker)+len(marker)]+b''.join(pieces)+final[final.index(end):]
    if raw!=reconstructed or composition.sha(raw)!=proof['output']['sha256']:
        errors.append('raw log provenance differs')
    renderer='redwood-gl: an OpenGL ES 3 context for CoreAnimation, by Mesa, off-screen (OSMesa): OpenGL ES 3.2 Mesa 25.0.7; FD725; shaders OpenGL ES GLSL ES 3.20\n'
    ready='redwood: marble GPU service ready before Mesa screen selection\n'
    if text.count(renderer)!=1 or text.count(ready)!=1:errors.append('hardware context selection differs')
    if len(re.findall(r'^redwood-gl: marble profile=(?:default|nofetch) share=1 fetch=0 blit=1 inputs=1 outputs=1$',text,re.M))!=1:
        errors.append('shared-input/no-fetch profile differs')
    if not re.search(r'^cmdline: .* gl=1 .* marble_gpu_runtime=1(?: |$)',text,re.M):
        errors.append('GPU compositor boot policy missing')
    def rows(prefix):
        return [dict((k,int(v,0)) for k,v in re.findall(r'(\w+)=(-?0x[0-9a-fA-F]+|-?\d+)',line))
                for line in text.splitlines() if line.startswith(prefix)]
    work=rows('marble-gpu-service: work ');submits=rows('marble-gpu-service: submit ')
    total=work[0].get('submissions',0) if len(work)==1 else 0
    if total<1000 or work!=[dict(PASS=1,stop_requested=1,submissions=total,pos=work[0].get('pos',-1),fence=67+total)] or not 0<=work[0].get('pos',-1)<4096:
        errors.append('completed GPU work missing')
    expected=list(range(1,9))+list(range(128,total+1,128))
    if [row.get('submissions') for row in submits]!=expected:errors.append('GPU samples missing/duplicate/reordered')
    for row in submits:
        if row.get('PASS')!=1 or row.get('fence')!=67+row.get('submissions',0) or not 1<=row.get('count',0)<=128 or not 0<=row.get('pos',-1)<4096 or not 1<=row.get('polls',0)<=100001:
            errors.append('GPU sample metadata differs')
    # Console writes from launchd can precede a complete kernel record on the
    # same line. Consume the exact, complete recorded suffix without rewriting
    # raw evidence or overlooking malformed/missing/duplicate generations.
    updates=context.map_records(text)
    leaves=0
    for generation,row in enumerate(updates,1):
        now=row.get('leaves',0)
        if row.get('generation')!=generation or row.get('PASS')!=1 or row.get('map') not in (0,1) or row.get('tlb_removed')!=1-row.get('map',0) or row.get('published',0)<1 or row.get('ranges',0)<1 or (row.get('map')==1 and now<=leaves) or (row.get('map')==0 and now>=leaves):
            errors.append('live BO map sequence differs')
        leaves=now
    if len(updates)<129:errors.append('live BO map evidence missing')
    if rows('marble-gpu-runtime: released ')!=[dict(PASS=1,draws=0,wraps=0,active=0)]:
        errors.append('audited GPU release missing')
    order=[text.find(value) for value in ['marble-gpu-service: MGPU-MARBLE-SERVICE-v1 ',ready,renderer,
           'marble-gpu-service: submit ','marble-gpu-service: work ','marble-gmu-gx: stopped ',
           'marble-gpu-context: stopped ','marble-gpu-runtime: released ']]
    if -1 in order or order!=sorted(order):errors.append('GPU lifecycle order differs')
    summaries=re.findall(r'^redwood-gl: after (\d+) frames: .*; (\d+) us a frame,.*; (\d+) errors$',text,re.M)
    if not summaries or int(summaries[-1][0])<600 or any(int(v[2]) for v in summaries):
        errors.append('compositor frames missing/errors')
    if any(v in text for v in ('panic(cpu','HEALTH_FAIL','completion timeout','SYSTEM_RESET',
                               'A SHADER DOES NOT COMPILE','A PROGRAM DOES NOT LINK','CALLED AND NOT THERE',
                               'NOT MADE','backdrop refused','backdrop copy failed','plain program link failure',
                               'marble-thermal: STOP requested',
                               'keeping software compositor','no accelerated renderer')):
        errors.append('render/kernel failure present')
    exercise=json.loads((directory/'exercise.json').read_text())
    if not all(exercise.get(v) is True for v in ('returned','gpu_ready','hardware_renderer')) or exercise.get('failure') is not None:
        errors.append('bounded display exercise failed')
    marks=[dict(name=name,seconds=int(t)/1e6) for name,t in re.findall(r'rwtouch: mark=([\w-]+) at_us=(\d+)',text)]
    if not marks or marks[-1]['name']!='complete':errors.append('gesture script incomplete')
    captures={name:composition.raw_capture(directory,name) for name in exercise['captured']}
    if 'ready' not in captures or exercise.get('dock',{}).get('white_regression_rejected') is not False:
        errors.append('settled white-Dock gate missing/failed')
    frames=recording.read_frames(directory/'recording.bin')
    review=json.loads((directory/'recording-review.json').read_text())
    if review['sha256']!=composition.sha((directory/'recording.bin').read_bytes()) or review['frames']!=len(frames):
        errors.append('recording review identity differs')
    start=marks[0]['seconds'];stop=marks[-1]['seconds'];hist=[]
    pattern=r'\[\s*([\d.]+)\]: MiniIOMobileFramebuffer: from one frame to the next: (\d+) times under 10 ms, (\d+) under 18, (\d+) under 26, (\d+) under 35, (\d+) longer \(the longest (\d+) ms\); half under (\d+) ms, 95 in 100 under (\d+) ms; (\d+) pauses'
    for match in re.finditer(pattern,text):
        timestamp=float(match[1]);values=list(map(int,match.groups()[1:]))
        # 5-second reporting windows wholly inside the same marked workload.
        if start+5<=timestamp<=stop:
            hist.append(dict(seconds=timestamp,bins=values[:5],longest_ms=values[5],p50_ms=values[6],p95_ms=values[7],pauses=values[8]))
    counts=[sum(row['bins'][i] for row in hist) for i in range(5)]
    errors[:]=list(dict.fromkeys(errors))
    report.update(passed=not errors,submissions=total,last_frame=int(summaries[-1][0]) if summaries else 0,
                  average_compositor_us=int(summaries[-1][1]) if summaries else 0,
                  captures=captures,pacing=exercise['pacing'],marks=marks,
                  recording=dict(frames=len(frames),start=frames[0][0],end=frames[-1][0],sha256=review['sha256'],
                                 max_near_black=max(row['black'] for row in review['frame_metrics']),
                                 max_settings_green=max(row['settings_green'] for row in review['frame_metrics'])),
                  timing=dict(windows=hist,bins=counts,under_35_fraction=sum(counts[:4])/sum(counts) if sum(counts) else None,
                              scope='software presenter intervals, excluding pauses; not optical refresh/FPS'),
                  visual_pass=None,transient_artifacts_pass=None,optical_display=False,in_test_temperature=False)
    return report


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('runs',nargs='+',type=Path)
    p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    runs=[inspect(path) for path in a.runs];passed=all(run['passed'] for run in runs)
    a.output.write_text(json.dumps(dict(passed=passed,scope='GPU lifecycle, raw evidence and bounded gestures; visual acceptance separate',runs=runs),indent=2)+'\n')
    for run in runs:print(run['log'],'PASS' if run['passed'] else 'FAIL',run['errors'])
    raise SystemExit(0 if passed else 1)


if __name__=='__main__':main()
