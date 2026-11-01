"""Catalog/extract ROM peripheral inputs on the existing read-only Linux extraction.

Does not load Linux modules, start firmware, or communicate with the phone.
"""
import argparse,importlib.util,json,shlex,tarfile,hashlib
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
REMOTE=r'''
import hashlib,json,re,struct,tarfile
from pathlib import Path
root=Path('/home/builder/rom-inspection');out=Path(OUTPUT);out.mkdir(parents=True,exist_ok=False)
def modinfo(path):
    d=path.read_bytes()
    if d[:6]!=b'\x7fELF\x02\x01':return {}
    sh=struct.unpack_from('<Q',d,40)[0];entry,count,idx=struct.unpack_from('<HHH',d,58)
    if entry!=64 or sh+entry*count>len(d):raise ValueError('module section table')
    sec=[struct.unpack_from('<IIQQQQIIQQ',d,sh+i*entry) for i in range(count)]
    strings=d[sec[idx][4]:sec[idx][4]+sec[idx][5]]
    result={}
    for row in sec:
        name=strings[row[0]:].split(b'\0')[0]
        if name!=b'.modinfo':continue
        for raw in d[row[4]:row[4]+row[5]].split(b'\0'):
            if b'=' not in raw:continue
            k,v=raw.decode(errors='replace').split('=',1);result.setdefault(k,[]).append(v)
    return result
modules=[];selected=set()
for f in sorted((root/'vendor_dlkm').rglob('*.ko')):
    info=modinfo(f);path=f.relative_to(root).as_posix()
    wanted=bool(re.search(r'cnss|wlan|wifi|mhi|qca|bluetooth|btpower|snd|sound|audio|haptic|sensors|slpi|battery|pmic|spmi|q6|gpr|apr|adsp',f.name,re.I))
    modules.append(dict(path=path,bytes=f.stat().st_size,sha256=hashlib.sha256(f.read_bytes()).hexdigest(),metadata=info,reference_selected=wanted))
    if wanted:selected.add(f)
for partition in ('vendor','odm'):
    for f in (root/partition/'etc').rglob('*'):
        if not f.is_file() or f.is_symlink():continue
        rel=f.relative_to(root).as_posix()
        if re.search(r'audio|mixer|acdb|bluetooth|sensors|wlan|wifi',rel,re.I) and f.stat().st_size<8*1024*1024:
            selected.add(f)
    for f in (root/partition/'firmware').rglob('*'):
        if not f.is_file() or f.is_symlink():continue
        rel=f.relative_to(root).as_posix()
        if re.search(r'wlan|qca|btfw|btfm|crbtfw|haptic|aw869',rel,re.I) and f.stat().st_size<16*1024*1024:
            selected.add(f)
manifest=[]
with tarfile.open(out/'material.tar.gz','w:gz',compresslevel=3) as tf:
    for f in sorted(selected):
        rel=f.relative_to(root).as_posix();data=f.read_bytes()
        manifest.append(dict(path=rel,bytes=len(data),sha256=hashlib.sha256(data).hexdigest()))
        tf.add(f,arcname=rel,recursive=False)
(out/'catalog.json').write_text(json.dumps(dict(modules=modules,material=manifest,loaded_in_xnu=False,
    module_scope='ELF/Linux ABI reference only; never dylib/kext payloads',
    source_scope='retained extraction of the user-provided N12Turbo ROM'),indent=2)+'\n')
print(json.dumps(dict(module_count=len(modules),selected_module_count=sum(x['reference_selected'] for x in modules),material_files=len(manifest),material_bytes=sum(x['bytes'] for x in manifest))))
'''
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--out',type=Path,required=True);a=p.parse_args();out=a.out.resolve()
    if not out.is_relative_to(ROOT/'out') or out.exists():p.error('new output under out required')
    out.mkdir(parents=True);guest='/home/builder/marble/'+out.relative_to(ROOT).as_posix()
    script=REMOTE.replace('OUTPUT',repr(guest));(out/'extract-source.py').write_text(script,encoding='utf8')
    spec=importlib.util.spec_from_file_location('material_vm',ROOT/'tools/marble-build-vm.py');vm=importlib.util.module_from_spec(spec);spec.loader.exec_module(vm);c=vm.connect()
    try:
        remote=guest+'-extract.py'
        with c.open_sftp() as s:s.put(str(out/'extract-source.py'),remote)
        _,stdout,_=c.exec_command('python3 '+shlex.quote(remote));stdout.channel.set_combine_stderr(True);log=stdout.read();code=stdout.channel.recv_exit_status();(out/'extract.log').write_bytes(log)
        if code:raise RuntimeError(log.decode(errors='replace'))
        with c.open_sftp() as s:
            for name in ('catalog.json','material.tar.gz'):s.get(guest+'/'+name,str(out/name))
    finally:c.close()
    catalog=json.loads((out/'catalog.json').read_text())
    expected={row['path']:row for row in catalog['material']};seen=set();material=out/'material'
    with tarfile.open(out/'material.tar.gz','r:gz') as tf:
        for member in tf:
            destination=(material/member.name).resolve()
            if not destination.is_relative_to(material) or not member.isfile() or member.name not in expected:raise ValueError('unsafe/unlisted archive entry')
            data=tf.extractfile(member).read();row=expected[member.name]
            if len(data)!=row['bytes'] or hashlib.sha256(data).hexdigest()!=row['sha256']:raise ValueError('material hash mismatch')
            destination.parent.mkdir(parents=True,exist_ok=True);destination.write_bytes(data);seen.add(member.name)
    if seen!=set(expected):raise ValueError('archive incomplete')
    print(log.decode(),end='');print('Host material extraction verified; firmware/modules NOT enabled in XNU')
if __name__=='__main__':main()
