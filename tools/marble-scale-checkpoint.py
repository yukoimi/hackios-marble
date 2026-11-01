"""Archive the scale candidate, rollback binaries and failed-test evidence.

Candidate checkpoint, never an assertion that stable 60 FPS was achieved.
"""
import argparse
from datetime import datetime,timezone
import importlib.util
import json
from pathlib import Path
import shutil
import tarfile

ROOT=Path(__file__).resolve().parents[1]


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('directory',type=Path);a=p.parse_args()
    target=a.directory.resolve()
    if not target.is_relative_to(ROOT/'out') or target.exists():p.error('use a new directory under out')
    spec=importlib.util.spec_from_file_location('scale_checkpoint_base',ROOT/'tools/marble-checkpoint.py')
    base=importlib.util.module_from_spec(spec);spec.loader.exec_module(base)
    target.mkdir(parents=True)
    files=['docs/MARBLE-GPU-SCALE-20261028.md','out/gpu-scale-v3-comparison.json','out/gpu-scale-v4-comparison.json',
        'out/daily-brightness-clock-v1-plan/loader-boot.img','out/daily-brightness-clock-v1-plan/loader-boot.json',
        'out/linux-build/mach-marble-gpu-cache-v4','out/linux-build/mach-marble-gpu-cache-v4.json',
        'out/marble-root-gpu-scale-v3.apfs','out/marble-root-gpu-scale-v3-control.apfs',
        'out/marble-root-gpu-scale-v3-pages.apfs','out/marble-root-gpu-scale-v3-2x-nofetch.apfs',
        'out/marble-root-gpu-scale-v4-layout.apfs']
    for directory in ('out/gpu-scale-v3-plan','out/gpu-scale-v3-verified-plan','out/gpu-scale-2x-loader-v3-plan','out/gpu-scale-v4-layout-plan'):
        files += [f.relative_to(ROOT).as_posix() for f in (ROOT/directory).rglob('*') if f.is_file()]
    evidence=['out/gpu-scale-v1-animation-live','out/gpu-scale-v2-animation-live',
        'out/gpu-scale-v3-animation-live','out/gpu-scale-v3-control-animation-live',
        'out/gpu-scale-v3-animation-retest-live','out/gpu-scale-v3-2x-animation-live',
        'out/gpu-scale-v3-2x-animation-retest-live','out/gpu-scale-v3-2x-pages-live',
        'out/gpu-scale-v3-2x-nofetch-animation-live','out/gpu-scale-v4-2x-layout-animation-live']
    for name in sorted(set(files)):
        destination=target/name;destination.parent.mkdir(parents=True,exist_ok=True)
        shutil.copy2(ROOT/name,destination)
    # Exact current source closure, including new untracked files. The tested
    # build snapshots above separately identify their own source versions.
    with tarfile.open(target/'sources.tar.gz','w:gz',compresslevel=3) as tf:
        for name,(path,mode) in sorted(base.sources().items()):
            if not path.is_file():raise FileNotFoundError(path)
            info=tf.gettarinfo(str(path),arcname=name);info.uid=info.gid=0;info.uname=info.gname=''
            info.mode=0o755 if mode=='100755' else 0o644
            if mode=='120000':
                info.type=tarfile.SYMTYPE;info.linkname=str(path.readlink()) if path.is_symlink() else path.read_text().strip();info.size=0;tf.addfile(info)
            else:
                with path.open('rb') as stream:tf.addfile(info,stream)
    evidence_manifest={}
    with tarfile.open(target/'evidence.tar.gz','w:gz',compresslevel=3) as tf:
        for directory in evidence:
            for f in sorted((ROOT/directory).rglob('*')):
                if not f.is_file():continue
                name=f.relative_to(ROOT).as_posix()
                evidence_manifest[name]=dict(bytes=f.stat().st_size,sha256=base.digest(f))
                tf.add(f,arcname=name,recursive=False)
    (target/'evidence-files.json').write_text(json.dumps(evidence_manifest,indent=2)+'\n',encoding='utf8')
    for name,entry in evidence_manifest.items():
        if base.digest(ROOT/name)!=entry['sha256']:raise RuntimeError('live evidence changed while archiving: '+name)
    # Validate archive contents through streaming hashes, not just the .gz hash.
    import hashlib
    with tarfile.open(target/'evidence.tar.gz','r:gz') as tf:
        for item in tf:
            h=hashlib.sha256();stream=tf.extractfile(item)
            for block in iter(lambda:stream.read(4*1024*1024),b''):h.update(block)
            if h.hexdigest()!=evidence_manifest[item.name]['sha256']:raise RuntimeError('archive entry mismatch: '+item.name)
    system=ROOT/'out/marble-system.apfs'
    metadata=dict(created_utc=datetime.now(timezone.utc).isoformat(),
        status='experimental candidate; 3x no demonstrated speed gain; 2x visual failures; stable 60 FPS incomplete',
        policy='fastboot boot only; RAM disks; no Android userdata; existing thermal/cap/idle guards retained',
        external_system=dict(path=str(system),bytes=system.stat().st_size,sha256=base.digest(system)),
        files={str(f.relative_to(target)):dict(bytes=f.stat().st_size,sha256=base.digest(f))
            for f in sorted(target.rglob('*')) if f.is_file()})
    (target/'manifest.json').write_text(json.dumps(metadata,indent=2)+'\n',encoding='utf8')
    print('Saved',len(metadata['files']),'files;',len(evidence_manifest),'evidence entries:',target,flush=True)


if __name__=='__main__':main()
