"""Build a finite RAM-only device writer and independent original-guard oracle.

No phone, partition, amplifier or USB operations. The oracle describes eight
bounded DMA pages, not speaker calibration or audible playback acceptance.
"""
import argparse,hashlib,importlib.util,json,re,shutil,subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def build(out,refill=False,stream=False):
 if refill and stream:raise ValueError("refill8 and stream24 are distinct profiles")
 out=out.resolve();out.mkdir(exist_ok=False)
 llvm=ROOT.parent/'toolchains/llvm-mingw-20261020-ucrt-x86_64/bin'
 paths=[ROOT/p for p in ('tools/marble-build-audio-signal.py','tools/marble-audio-signal.h','tools/marble-audio-signal-test.c','tools/tests/marble-audio-signal-oracle.c','tools/marble-build-brightness.py','tools/marble-thermal-idle-image.py','xnu/files/bsd/dev/redwood_guard.c','xnu/files/bsd/sys/redwood_audio.h','xnu/files/bsd/sys/marble_audio_pcm_probe.h','xnu/files/bsd/sys/marble_audio_queue.h')]
 if stream:paths += [ROOT/'tools/marble-audio-stream-test.c',ROOT/'xnu/files/bsd/sys/marble_audio_refill.h']
 if refill:paths += [ROOT/'tools/marble-audio-refill-test.c',ROOT/'xnu/files/bsd/sys/marble_audio_refill.h']
 paths+=list((ROOT/'userland/libmini').glob('*'))+list((ROOT/'tools/apfs').glob('*.py'))
 sources={p.relative_to(ROOT).as_posix():sha(p) for p in paths if p.is_file()};commands=[]
 with (out/'build.log').open('w',encoding='utf8') as log:
  def run(args):
   args=list(map(str,args));commands.append(args);(out/'commands.json').write_text(json.dumps(commands,indent=2))
   r=subprocess.run(args,cwd=ROOT,capture_output=True,text=True,errors='replace');log.write(r.stdout+r.stderr);log.flush()
   if r.returncode:raise RuntimeError(r.stdout+r.stderr)
   return r.stdout
  flags=['--target=arm64-apple-ios16.5','-Os','-ffreestanding','-fno-builtin','-fno-stack-protector','-Wall','-Wextra','-Iuserland/libmini']
  objects=[]
  for name,source in [('test','tools/marble-audio-stream-test.c' if stream else 'tools/marble-audio-refill-test.c' if refill else 'tools/marble-audio-signal-test.c'),('mini','userland/libmini/mini.c'),('start','userland/libmini/start.S')]:
   obj=out/(name+'.o');objects.append(obj);run([llvm/'clang.exe',*flags,'-c',source,'-o',obj])
  linked=out/'test.linked'
  run([llvm/'ld.lld.exe','-flavor','darwin','-arch','arm64','-no_fixup_chains','-no_adhoc_codesign','-headerpad','0x1000','-e','_start','-pagezero_size','0x100000000','-platform_version','ios','16.5','16.5','-o',linked,*objects])
  spec=importlib.util.spec_from_file_location('signal_static_entry',ROOT/'tools/marble-build-brightness.py');module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
  module.static_entry(linked,out/'signal-test')
  run(['python','tools/marble-thermal-idle-image.py','--program',out/'signal-test','--out',out/'test.apfs'])
  metadata=json.loads((out/'test.json').read_text());metadata.update(scope='finite static PCM device writer and RAM-copy check; not iOS or sleeping-init',hardware_verified=False,audible=False)
  (out/'test.json').write_text(json.dumps(metadata,indent=2)+'\n')
  inc=out/'include/sys';inc.mkdir(parents=True);shutil.copy2(ROOT/'xnu/files/bsd/sys/redwood_audio.h',inc/'redwood_audio.h');(inc/'ioccom.h').write_text('#define _IOW(g,n,t) 0\n')
  run([llvm/'clang.exe','-O2','-Wall','-DKERNEL',*(['-DMARBLE_SIGNAL_STREAM24=1'] if stream else ['-DMARBLE_SIGNAL_REFILL=1'] if refill else []),'-I'+str(out/'include'),'tools/tests/marble-audio-signal-oracle.c','xnu/files/bsd/dev/redwood_guard.c','-o',out/'signal-oracle.exe'])
  text=run([out/'signal-oracle.exe']);(out/'oracle.log').write_text(text)
  pages=[]
  for line in text.splitlines():
   r=dict(re.findall(r'(\w+)=([^\s]+)',line));pages.append(dict(period=int(r['period']),peak=int(r['peak']),nonzero=int(r['nonzero']),hash=r['hash']))
  if len(pages)!=(24 if stream else 8) or [r['period'] for r in pages]!=list(range(24 if stream else 8)):raise ValueError('oracle period ordering')
  if any((r['peak']!=0 or r['nonzero']!=0) if (refill or stream) and r['period']==2 else (0>=r['peak'] or r['peak']>64 or not 400<r['nonzero']<=512) for r in pages):raise ValueError('oracle is silent or out of bounds')
  oracle=dict(pattern='triangle1k-stream24-gap' if stream else 'triangle1k-refill-gap' if refill else 'triangle1k',frames=5888 if stream else 1792 if refill else 2048,rate=48000,bits=16,channels=2,raw_peak=504,guard='original',HPF=True,extra_divisor=16,peak_bound=64,pages=pages,hardware_verified=False,audible=False)
  if stream:oracle.update(output_frames=6144,silent_periods=[2],prebuffer_frames=512,late_frames=5376,periods=24,producer_writes=19,max_queue_frames=2048)
  if refill:oracle.update(output_frames=2048,silent_periods=[2],prebuffer_frames=512,late_frames=1280)
  (out/'oracle.json').write_text(json.dumps(oracle,indent=2)+'\n')
 if any(sha(ROOT/name)!=digest for name,digest in sources.items()):raise ValueError('signal source changed while building')
 for name in sources:
  dest=out/'source'/name;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(ROOT/name,dest)
 (out/'sources.json').write_text(json.dumps(sources,indent=2)+'\n')
 result=dict(status='OFFLINE_BUILT_NOT_HARDWARE_QUALIFIED',disk_sha256=sha(out/'test.apfs'),oracle_sha256=sha(out/'oracle.json'),program_sha256=sha(out/'signal-test'),disk_bytes=(out/'test.apfs').stat().st_size,audio_acceptance=False,scope='finite device writer and original-guard oracle; no phone operations')
 (out/'manifest.json').write_text(json.dumps(result,indent=2)+'\n');return result
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--out',type=Path,required=True);p.add_argument('--refill',action='store_true');p.add_argument('--stream24',action='store_true');a=p.parse_args();print(json.dumps(build(a.out,a.refill,a.stream24),indent=2))
if __name__=='__main__':main()
