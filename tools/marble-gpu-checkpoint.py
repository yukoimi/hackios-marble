"""Preserve the validated GPU compositor separately from earlier software baselines."""
import argparse, importlib.util, json, shutil, subprocess, tarfile
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('checkpoint', ROOT / 'tools/marble-checkpoint.py')
checkpoint = importlib.util.module_from_spec(spec)
spec.loader.exec_module(checkpoint)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('directory', type=Path)
    a = p.parse_args()
    target = a.directory.resolve()
    items = [
        'out/marble-gpu-service-loader-boot.img', 'out/marble-gpu-service-loader-boot.json',
        'out/linux-build/mach-marble-gpu-service-v2', 'out/linux-build/mach-marble-gpu-service-v2.json',
        'out/gpu-service-v2-source.tar.gz', 'out/gpu-accelerated-userland-source.tar.gz',
        'out/marble-root-gpu-accelerated-launchd.apfs',
        'out/linux-build/userland/libOSMesa.8.dylib',
        'out/linux-build/userland/libredwood-gl.dylib', 'out/linux-build/userland/libredwood.dylib',
        'out/gpu-service-v2-cpu-live/returned.log', 'out/marble-gpu-service-report.json',
        'out/marble-gpu-composition-report.json', 'out/marble-gpu-visual-review.json',
        'out/gpu-composition-first-returned.log', 'out/gpu-composition-sysmem-returned.log',
        'out/20261024-234941-marble-live.log', 'out/20261024-235010-marble-live.log',
        'out/20261024-235127-marble-live.log', 'out/marble-gpu-final-device-state.json',
    ]
    for name in ('ui-gpu-copy-pages', 'ui-gpu-copy-input-pages', 'ui-gpu-accelerated-pages',
                 'ui-gpu-accelerated-basic', 'ui-gpu-composition', 'ui-gpu-sysmem-pages',
                 'ui-gmu-hfi'):
        items += [str(path.relative_to(ROOT)) for path in (ROOT / 'out' / name).rglob('*') if path.is_file()]
    for directory in ('tools', 'docs'):
        items += [str(path.relative_to(ROOT)) for path in (ROOT / directory).rglob('*')
                  if path.is_file() and '__pycache__' not in path.parts]
    # Check missing inputs before creating an incomplete recovery directory.
    for item in items:
        if not (ROOT / item).is_file():
            raise FileNotFoundError(item)
    for report in ('out/marble-gpu-service-report.json', 'out/marble-gpu-composition-report.json'):
        if json.loads((ROOT / report).read_text())['passed'] is not True:
            raise ValueError('Required validation failed: ' + report)
    system = ROOT / 'out/marble-system.apfs'
    external = dict(path=str(system), bytes=system.stat().st_size, sha256=checkpoint.digest(system),
                    copied=False, purpose='Existing RAM system APFS; required to upload after a fresh loader boot')
    target.mkdir(parents=True, exist_ok=False)
    for item in sorted(set(items)):
        destination = target / item
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(ROOT / item, destination)
    # Host source snapshot complements exact guest kernel and userland archives.
    with tarfile.open(target / 'host-sources.tar.gz', 'w:gz') as tf:
        for name, (path, mode) in sorted(checkpoint.sources().items()):
            if not path.is_file():
                raise FileNotFoundError(path)
            info = tf.gettarinfo(str(path), arcname=name)
            info.uid = info.gid = 0
            info.uname = info.gname = ''
            info.mode = 0o755 if mode == '100755' else 0o644
            if mode == '120000':
                info.type = tarfile.SYMTYPE
                info.linkname = str(path.readlink()) if path.is_symlink() else path.read_text().strip()
                info.size = 0
                tf.addfile(info)
            else:
                with path.open('rb') as f:
                    tf.addfile(info, f)
    (target / 'kernel.patch').write_bytes(checkpoint.git(ROOT, 'diff', '--binary', 'HEAD'))
    readme = '''# marble GPU accelerated recovery point

RAM only. Never flash, erase, unlock or access Android /data.
This is an experimental FD725 CoreAnimation renderer, not complete Metal or app compatibility.

Verify all copied files:
    python tools/marble-checkpoint.py . --verify

Recheck raw evidence using only this recovery directory:
    python tools/marble-gpu-service-report.py out/gpu-service-v2-cpu-live/returned.log --output shader-rechecked.json
    python tools/marble-gpu-composition-report.py out/ui-gpu-copy-input-pages out/ui-gpu-accelerated-pages --reference out/ui-gpu-copy-pages --output compositor-rechecked.json
    python tools/marble-gpu-visual-review.py

These tools require Python and Pillow. Do not overwrite original evidence reports.
The visual-review tool reproduces the independent GPU comparison, while the old software-reference report remains failed (menu appearance).

See docs/MARBLE-GPU.md final section for commands. ADB/fastboot are external tools.
The 2.6 GB marble-system.apfs is NOT copied here; its original local path, size and SHA256 are in manifest.external_dependencies.
Verify that dependency before use, then upload it with the RAM-only ramdisk command.
The original Mu image and ROM are not needed just to boot the saved loader/kernel/support disk.
Source rebuild additionally needs the SDK/toolchain/Mesa dependencies described in docs/MARBLE-BUILD.md; this is not a bundled build VM.

Exact guest sources: out/gpu-service-v2-source.tar.gz and out/gpu-accelerated-userland-source.tar.gz.
host-sources.tar.gz and kernel.patch preserve the current host integration/tools, not a claim that later edits built the matching kernel.
The kernel sidecar still says candidate from build time; subsequent hardware reports are the validation evidence.
The phone was returned to Android after a complete GPU drain and loader return.
'''
    (target / 'RESTORE.md').write_text(readme, encoding='utf8')
    manifest = dict(created_utc=datetime.now(timezone.utc).isoformat(),
                    policy='Temporary fastboot boot; RAM APFS; no Android userdata access',
                    acceleration_enabled=True, renderer='FD725 / Mesa 25.0.7 / CoreAnimation GLES',
                    kernel_base=checkpoint.git(ROOT, 'rev-parse', 'HEAD').decode().strip(),
                    userland_base=checkpoint.git(ROOT.parent / 'hackios-xnu-userland', 'rev-parse', 'HEAD').decode().strip(),
                    limits='Sampled framebuffer and synthetic input; no optical/physical touch or in-test temperature certification. No Metal claim.',
                    external_dependencies=[external], files={})
    for path in sorted(target.rglob('*')):
        if path.is_file():
            manifest['files'][path.relative_to(target).as_posix()] = dict(bytes=path.stat().st_size, sha256=checkpoint.digest(path))
    (target / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf8')
    subprocess.run(['python', str(ROOT / 'tools/marble-checkpoint.py'), str(target), '--verify'], check=True)
    print('Saved validated GPU recovery point:', target)


if __name__ == '__main__':
    main()
