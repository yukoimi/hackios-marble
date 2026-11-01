"""Validate actual CoreAnimation/FD725 work, exact raw logs and reviewed UI captures."""
import argparse,hashlib,importlib.util,json,re
from pathlib import Path,PureWindowsPath
from PIL import Image,ImageChops,ImageStat
s=importlib.util.spec_from_file_location('blit',Path(__file__).with_name('marble-gpu-blit-report.py'))
blit=importlib.util.module_from_spec(s);s.loader.exec_module(blit)
ACTIONS=['today','today-home','page2','library','library-back','home','menu','dismiss']
def sha(raw):return hashlib.sha256(raw).hexdigest()
def evidence_path(value,directory):
    # Provenance retains original absolute paths. Resolve only its matching
    # run-local files so a verified recovery copy can be checked independently.
    p=PureWindowsPath(value)
    if p.parent.name==directory.name:return directory/p.name
    if p.parent.name=='logs' and p.parent.parent.name==directory.name:return directory/'logs'/p.name
    raise ValueError('provenance source outside this run: '+value)
def image_difference(path,reference):
    x=Image.open(path).convert('RGB');y=Image.open(reference).convert('RGB')
    if x.size!=(1080,2400) or y.size!=x.size:raise ValueError('frame dimensions differ')
    diff=ImageChops.difference(x.crop((0,130,1080,2400)),y.crop((0,130,1080,2400)))
    mean=sum(ImageStat.Stat(diff).mean)/3
    channels=diff.split();maximum=ImageChops.lighter(ImageChops.lighter(channels[0],channels[1]),channels[2])
    fraction=sum(maximum.histogram()[25:])/(1080*2270)
    return dict(mean_error=mean,pixels_over_24_fraction=fraction,passed=mean<3 and fraction<.02)
def raw_capture(directory,name):
    meta=json.loads((directory/(name+'.json')).read_text())
    raw=(directory/(name+'.bgra')).read_bytes()
    if sha(raw)!=meta['sha256'] or meta['width']!=1080 or meta['height']!=2400:raise ValueError('raw capture metadata differs: '+name)
    if len(raw)%(2400*4):raise ValueError('frame stride differs')
    decoded=Image.frombytes('RGBA',(1080,2400),raw,'raw','BGRA',len(raw)//2400).convert('RGB')
    png=Image.open(directory/(name+'.png')).convert('RGB')
    if ImageChops.difference(decoded,png).getbbox() is not None:raise ValueError('PNG differs from raw framebuffer: '+name)
    return dict(raw_sha256=meta['sha256'],png_sha256=sha((directory/(name+'.png')).read_bytes()))
def inspect(directory,reference):
    path=directory/'complete.log';r=blit.inspect(path,live='service');errors=r['errors'];raw=path.read_bytes();text=raw.decode('utf8')
    proof=json.loads(path.with_suffix('.provenance.json').read_text())
    pieces=[]
    for v in proof['records']:
        source=evidence_path(v['path'],directory).read_bytes()
        if sha(source)!=v['sha256']:errors.append('raw log source hash differs')
        overlap=v['overlap_bytes'];at=v['copy_offset']
        if overlap and (overlap<1024 or source[at-overlap:at]!=b''.join(pieces)[-overlap:]):errors.append('raw log continuity differs')
        pieces.append(source[v['copy_offset']:v['copy_offset']+v['copy_bytes']])
    final=evidence_path(proof['header_source'],directory).read_bytes();marker=b'== KERNEL LOG ==\n';end=b'\n== END OF LOG ==\n'
    reconstructed=final[:final.index(marker)+len(marker)]+b''.join(pieces)+final[final.index(end):]
    if raw!=reconstructed or sha(raw)!=proof['output']['sha256']:errors.append('exact raw log provenance differs')
    def rows(prefix):
        return [dict((k,int(v,0)) for k,v in re.findall(r'(\w+)=(-?0x[0-9a-fA-F]+|-?\d+)',line))
            for line in text.splitlines() if line.startswith(prefix)]
    if not re.search(r'^cmdline: .* gl=1 .* marble_gpu_runtime=1$',text,re.M):errors.append('GPU compositor boot policy missing')
    ready='redwood: marble GPU service ready before Mesa screen selection\n'
    renderer='redwood-gl: an OpenGL ES 3 context for CoreAnimation, by Mesa, off-screen (OSMesa): OpenGL ES 3.2 Mesa 25.0.7; FD725; shaders OpenGL ES GLSL ES 3.20\n'
    if text.count(ready)!=1 or text.count(renderer)!=1 or text.find(ready)>text.find(renderer):errors.append('hardware compositor/context selection differs')
    if len(re.findall(r'^redwood-gl: marble profile=(?:default|copy-input) share=1 fetch=1 blit=1 inputs=0 outputs=1$',text,re.M))!=1:errors.append('input upload/shared output profile missing')
    work=rows('marble-gpu-service: work ');submits=rows('marble-gpu-service: submit ')
    total=work[0].get('submissions',0) if len(work)==1 else 0
    if total<1000 or work!=[dict(PASS=1,stop_requested=1,submissions=total,pos=work[0].get('pos',-1),fence=67+total)] or not 0<=work[0].get('pos',-1)<4096:errors.append('GPU work/final fence missing')
    expected=list(range(1,9))+list(range(128,total+1,128))
    if [v.get('submissions') for v in submits]!=expected:errors.append('sampled submissions missing/duplicate/reordered')
    for v in submits:
        if v.get('PASS')!=1 or v.get('fence')!=67+v.get('submissions',0) or not 1<=v.get('count',0)<=128 or not 0<=v.get('pos',-1)<4096 or not 1<=v.get('polls',0)<=100001:errors.append('submission/fence metadata differs')
    updates=rows('marble-gpu-live-map: map=');leaves=0
    for generation,v in enumerate(updates,1):
        now=v.get('leaves',0)
        if v.get('generation')!=generation or v.get('PASS')!=1 or v.get('map') not in (0,1) or v.get('tlb_removed')!=1-v.get('map',0) or v.get('published',0)<1 or v.get('ranges',0)<1 or (v.get('map')==1 and now<=leaves) or (v.get('map')==0 and now>=leaves):errors.append('live BO map sequence differs')
        leaves=now
    if len(updates)<129:errors.append('bring-up/live allocation evidence missing')
    release=rows('marble-gpu-runtime: released ')
    if release!=[dict(PASS=1,draws=0,wraps=0,active=0)]:errors.append('audited release missing')
    ordering=[text.find(v) for v in ['marble-gpu-service: MGPU-MARBLE-SERVICE-v1 ',ready,renderer,'marble-gpu-service: submit ','marble-gpu-service: work ','marble-gmu-gx: stopped ','marble-gpu-context: stopped ','marble-gpu-runtime: released ']]
    if -1 in ordering or ordering!=sorted(ordering):errors.append('renderer/submission/stop lifecycle order differs')
    frames=re.findall(r'^redwood-gl: frame (\d+): .*; (\d+) pictures? copied out, (\d+) drawn in place; (\d+) us ',text,re.M)
    summaries=re.findall(r'^redwood-gl: after (\d+) frames: .*; (\d+) us a frame,.*; (\d+) errors$',text,re.M)
    if not summaries or int(summaries[-1][0])<600 or any(int(v[2]) for v in summaries) or not any(int(v[2])>0 and int(v[1])==0 for v in frames):errors.append('completed shared-output frame evidence missing/errors')
    if any(v in text for v in ('panic(cpu','HEALTH_FAIL','completion timeout','SYSTEM_RESET','A SHADER DOES NOT COMPILE','A PROGRAM DOES NOT LINK','CALLED AND NOT THERE')):errors.append('render/kernel failure present')
    exercise=json.loads((directory/'exercise.json').read_text());names=exercise['frames'];checks=[]
    rounds=len(names)//len(ACTIONS)
    if exercise.get('returned') is not True or exercise.get('software') is not False or not 2<=rounds<=4 or names!=[f'{i:02}-{name}' for i in range(rounds) for name in ACTIONS]:errors.append('bounded UI exercise incomplete')
    for name in names:
        ref='00-'+name[3:]
        capture=raw_capture(directory,name);reference_capture=raw_capture(reference,ref)
        difference=image_difference(directory/(name+'.png'),reference/(ref+'.png'))
        checks.append(dict(name=name,**capture,reference=reference_capture,**difference))
    if not all(v['passed'] for v in checks):errors.append('reviewed full-copy GPU reference differs')
    r.update(passed=not errors,submissions=total,last_frame=int(summaries[-1][0]) if summaries else 0,
        average_compositor_us=int(summaries[-1][1]) if summaries else 0,image_checks=checks,
        last_live_leaves=leaves,limits='Sampled settled framebuffer captures and synthetic input; no optical display, physical touch or temperature measurement. Timings are compositor work, not displayed FPS.')
    return r
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('runs',nargs='+',type=Path)
    p.add_argument('--reference',required=True,type=Path);p.add_argument('--output',required=True,type=Path);a=p.parse_args()
    runs=[inspect(path,a.reference) for path in a.runs];passed=all(r['passed'] for r in runs)
    a.output.write_text(json.dumps(dict(passed=passed,ios_gpu_composition_enabled=passed,renderer='FD725',runs=runs),indent=2)+'\n')
    for r in runs:print(r['log'],'PASS' if r['passed'] else 'FAIL',r['errors'])
    raise SystemExit(0 if passed else 1)
if __name__=='__main__':main()
