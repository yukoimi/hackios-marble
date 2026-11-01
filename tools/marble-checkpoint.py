#!/usr/bin/env python3
"""Save a local, independently verifiable RAM-only marble recovery baseline."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tarfile

ROOT = Path(__file__).resolve().parents[1]


def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as f:
        for block in iter(lambda: f.read(4 * 1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()


def git(repo, *args):
    return subprocess.check_output(['git', '-C', str(repo), *args])


def sources():
    """Archive both repositories, taking merged userland files from this workspace."""
    entries = {}
    userland = ROOT.parent / 'hackios-xnu-userland'
    for repo in (userland, ROOT):
        for record in git(repo, 'ls-files', '--stage', '-z').split(b'\0'):
            if not record:
                continue
            info, name = record.split(b'\t', 1)
            mode = info.split()[0].decode()
            name = name.decode('utf8')
            # Root metadata is supplied by the kernel repository.
            if repo == userland and name in ('README.md', 'LICENSE', '.gitignore'):
                continue
            path = ROOT / name
            if not path.exists():
                path = repo / name
            entries[name] = (path, mode)
    for raw in git(ROOT, 'ls-files', '--others', '--exclude-standard', '-z').split(b'\0'):
        if raw:
            name = raw.decode('utf8')
            entries[name] = (ROOT / name, '100755' if name.endswith('.sh') else '100644')
    return entries


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('directory', type=Path)
    p.add_argument('--verify', action='store_true')
    p.add_argument('--source-build', action='store_true', help='save the hardware-validated source kernel and source desktop profile')
    a = p.parse_args()
    target = a.directory.resolve()
    manifest = target / 'manifest.json'
    if a.verify:
        data = json.loads(manifest.read_text(encoding='utf8'))
        for name, entry in data['files'].items():
            path = (target / name).resolve()
            if not path.is_relative_to(target) or digest(path) != entry['sha256']:
                raise RuntimeError('baseline mismatch: ' + name)
        print('Verified', len(data['files']), 'baseline files:', target)
        return
    target.mkdir(parents=True, exist_ok=False)
    data = {'created_utc': datetime.now(timezone.utc).isoformat(),
            'policy': 'fastboot boot only; RAM APFS; no Android userdata access',
            'kernel_base': git(ROOT, 'rev-parse', 'HEAD').decode().strip(),
            'userland_base': git(ROOT.parent / 'hackios-xnu-userland', 'rev-parse', 'HEAD').decode().strip(),
            'validation': {
                'hardware_tested_boot_sha256': '92092c2526aa677113f4f36b852c396356fa3d037d8be2c51b710dcd5279cfc1',
                'saved_boot_status': 'rebuilt with same runtime fix; needs a fresh hardware boot',
                'touch': 'user confirmed; 4234 valid reports, bad=0; release lag 0-2 ms',
                'cpu': 'CPU7,6,5,4; 36 checks passed; measured 3.8 concurrent cores'},
            'files': {}}
    if a.source_build:
        data['validation']={
            'saved_boot_status':'hardware validated with source kernel; RAM-only',
            'hardware_tested_boot_sha256':digest(ROOT/'out/marble-source-loader-boot.img'),
            'kernel_sha256':digest(ROOT/'out/linux-build/mach.marble-source'),
            'cpu':'36 checks passed; four-core 60-second stress PASS; timer drift 0; secondaries OFF',
            'touch':'user confirmed source desktop touch/display normal and no abnormal heating',
            'display':'fb_threads=2; six-swipe comparisons saved for 1/2/4; GPU disabled',
            'limits':'only four big cores; no DVFS; requested clock only; no temperature telemetry'}
    archive = target / 'sources.tar.gz'
    with tarfile.open(archive, 'w:gz') as tf:
        for name, (path, mode) in sorted(sources().items()):
            if not path.is_file():
                raise RuntimeError('missing source: ' + str(path))
            info = tf.gettarinfo(str(path), arcname=name)
            info.uid = info.gid = 0
            info.uname = info.gname = ''
            info.mode = 0o755 if mode == '100755' else 0o644
            if mode == '120000':
                info.type = tarfile.SYMTYPE
                info.linkname = path.read_text().strip() if not path.is_symlink() else str(path.readlink())
                info.size = 0
                tf.addfile(info)
            else:
                with path.open('rb') as f:
                    tf.addfile(info, f)
    (target / 'kernel.patch').write_bytes(git(ROOT, 'diff', '--binary', 'HEAD'))
    names=('marble-touch-fixed-boot.img', 'marble-touch-fixed-boot.json',
                 'loader-marble-touch-fixed.efi', 'marble-root-launchd-touch-fixed.apfs',
                 'marble-touch-bridge-fixed', 'marble-input-qos-fixed.dylib',
                 'marble-touch-fixed-live.log', 'marble-system.apfs',
                 'marble-smp-1-cli.log', 'marble-smp-2-cli.log', 'marble-smp-4-cli.log')
    if a.source_build:
        names=('marble-source-loader-boot.img','marble-source-loader-boot.json',
               'marble-root-launchd-source.apfs','marble-root-launchd-source-benchmark.apfs',
               'marble-root-cli.apfs','marble-system.apfs','marble-source-kernel-cli.log',
               'marble-source-desktop-live.log','marble-source-regression-report.json',
               'marble-source-fb1.log','marble-source-fb2.log','marble-source-fb4.log',
               'marble-source-fb1-perf.json','marble-source-fb2-perf.json','marble-source-fb4-perf.json')
        shutil.copytree(ROOT/'out/linux-build',target/'linux-build',
                        ignore=shutil.ignore_patterns('hostbin'))
    for name in names:
        shutil.copyfile(ROOT / 'out' / name, target / name)
    for path in sorted(target.rglob('*')):
        if path.is_file():
            data['files'][path.relative_to(target).as_posix()] = {'bytes': path.stat().st_size, 'sha256': digest(path)}
    manifest.write_text(json.dumps(data, indent=2) + '\n', encoding='utf8')
    print('Saved', len(data['files']), 'baseline files:', target)


if __name__ == '__main__':
    main()
