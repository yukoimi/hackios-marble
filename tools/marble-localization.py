"""Extract a separate, size-verified Simplified Chinese overlay from the retained local DMG.

No phone access. Existing English inputs and system image are preserved.
"""
import argparse, hashlib, importlib.util, json, os, re, stat, subprocess
from pathlib import Path
from marble_windows import extracted_path
ROOT=Path(__file__).resolve().parents[1]
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--out',type=Path,required=True)
    p.add_argument('--unzbm',type=Path,required=True);a=p.parse_args();out=a.out.resolve()
    if not out.is_relative_to(ROOT/'out') or out.exists():p.error('new output under out required')
    out.mkdir(parents=True)
    spec=importlib.util.spec_from_file_location('select_zh',ROOT/'ios165/select-system.py')
    select=importlib.util.module_from_spec(spec);spec.loader.exec_module(select)
    select.KEEP_LANG+=('zh-Hans','zh_CN','zh-Hans-CN')
    chosen={};sources={};meta={}
    for e in select.entries(ROOT/'ios165/rootfs.slt'):
        if e.get('Alternate Stream')=='+':continue
        source=e['Path'];path=source
        if path.startswith(select.STAGED_APPS):path='Applications/'+path[len(select.STAGED_APPS):]
        elif path.startswith('private/var/'):continue
        mode=select.mode_bits(e['Mode'])
        if mode is None:continue
        meta[path]=(mode,int(e.get('User ID') or 0),int(e.get('Group ID') or 0),e.get('Symbolic Link',''))
        lang=re.search(r'/([^/]+)\.lproj(?:/|$)',path)
        if not lang or lang.group(1) not in ('zh-Hans','zh_CN','zh-Hans-CN'):continue
        size=int(e.get('Size') or 0)
        keep=select.wanted_file(path,size) if stat.S_ISREG(mode) else path.startswith(select.WHOLE) and not path.startswith(select.SKIP_PREFIX)
        if keep:
            chosen[path]=meta[path]
            if stat.S_ISREG(mode):sources[path]=(source,size)
    for path in list(chosen):
        while '/' in path:
            path=path.rsplit('/',1)[0]
            if path in meta:chosen[path]=meta[path]
    files=out/'system.files';files.write_text(''.join(source+'\n' for source,_ in sources.values()),encoding='utf8')
    manifest=out/'system.manifest'
    manifest.write_text(''.join(f'{m:o}\t{u}\t{g}\t{path}\t{target}\n' for path,(m,u,g,target) in sorted(chosen.items())),encoding='utf8')
    dest=out/'rootfs';dest.mkdir()
    cmd=[str(ROOT.parent/'toolchains/7zip/7z.exe'),'x','-y','-sns','-snc','-scsUTF-8','-o'+str(dest),str(ROOT/'ios165/rootfs.dmg.part'),'@'+str(files)]
    with (out/'extract.log').open('wb') as log:result=subprocess.run(cmd,stdout=log,stderr=log)
    if result.returncode not in (0,1,2):raise RuntimeError('7zip failed; see log')
    compressed=[('\\\\?\\' if os.name=='nt' else '')+str(f.resolve()) for f in dest.rglob('*') if f.is_file() and Path(str(f)+':com.apple.decmpfs').exists()]
    for i in range(0,len(compressed),15):subprocess.run([str(a.unzbm.resolve()),*compressed[i:i+15]],check=True)
    verified=[]
    for path,(source,size) in sorted(sources.items()):
        f=extracted_path(dest,source)
        if not f.is_file() or f.stat().st_size!=size:raise RuntimeError('incomplete Chinese resource '+str(f))
        verified.append(dict(path=path,source=source,bytes=size,sha256=hashlib.sha256(f.read_bytes()).hexdigest()))
    (out/'verified.json').write_text(json.dumps(dict(files=verified,total_bytes=sum(v['bytes'] for v in verified),entries=len(chosen),language='zh-Hans'),indent=2)+'\n')
    print(f'Chinese overlay: {len(verified)} files, {sum(v["bytes"] for v in verified)} bytes, {len(compressed)} decoded')
if __name__=='__main__':main()
