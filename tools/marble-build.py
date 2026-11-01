#!/usr/bin/env python3
"""Build the marble EFI and RAM boot image with a supplied LLVM/7zip SDK."""
import argparse
from pathlib import Path
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--llvm', type=Path, required=True, help='LLVM bin directory')
    p.add_argument('--lzma-sdk', type=Path, required=True)
    p.add_argument('--source', type=Path, required=True)
    a = p.parse_args()
    clang = str(a.llvm.resolve() / 'clang.exe')
    link = str(a.llvm.resolve() / 'ld.lld.exe')
    sdk = a.lzma_sdk.resolve()
    def run(*cmd):
        # Windows scanners can briefly lock an executable just after linking.
        # Retry only process creation; never retry a failed test assertion.
        for attempt in range(11):
            try:
                subprocess.run(list(map(str, cmd)), cwd=ROOT, check=True)
                return
            except PermissionError:
                if attempt == 10:
                    raise
                time.sleep(0.5)
    (ROOT/'out').mkdir(exist_ok=True)
    flags = ['--target=aarch64-unknown-windows', '-ffreestanding', '-fno-builtin',
             '-fno-stack-protector', '-fshort-wchar', '-mgeneral-regs-only', '-O1',
             '-Wall', '-Wextra', '-Wno-unused-parameter']
    for name, defines in [('marble', ['-DXNU_MARBLE=1']), ('generic', [])]:
        run(clang, *flags, *defines, '-c', 'xnuloader/loader.c', '-o', f'out/loader-{name}.obj')
        run(link, '-flavor', 'link', '/subsystem:efi_application', '/entry:efi_main',
            '/nodefaultlib', '/base:0xA1000000', f'/out:out/loader-{name}.efi', f'out/loader-{name}.obj')
    run(clang, '-O2', '-DZ7_ST', '-I'+str(sdk), 'tools/marble-lzma.c',
        *[sdk/f for f in ('LzmaEnc.c','LzFind.c','LzFindOpt.c','CpuArch.c','Alloc.c')],
        '-o', 'out/marble-lzma.exe')
    run(clang, '-O2', 'tools/tests/marble-policy.c', '-o', 'out/marble-policy-test.exe')
    run(ROOT/'out/marble-policy-test.exe')
    run(clang, '-O2', 'tools/tests/marble-cpucap.c', '-o', 'out/marble-cpucap-test.exe')
    run(ROOT/'out/marble-cpucap-test.exe')
    run(clang, '-O2', 'tools/tests/marble-efi-idle.c', '-o', 'out/marble-efi-idle-test.exe')
    run(ROOT/'out/marble-efi-idle-test.exe')
    run(clang, '-O2', 'tools/tests/marble-thermal-window.c', '-o', 'out/marble-thermal-window-test.exe')
    run(ROOT/'out/marble-thermal-window-test.exe')
    run(clang, '-O2', 'tools/tests/marble-touch-poll.c', '-o', 'out/marble-touch-poll-test.exe')
    run(ROOT/'out/marble-touch-poll-test.exe')
    run(clang, '-O2', 'tools/tests/marble-gpu-power.c', '-o', 'out/marble-gpu-power-test.exe')
    run(ROOT/'out/marble-gpu-power-test.exe')
    run(clang, '-O2', 'tools/tests/marble-smmu-fence.c', '-o', 'out/marble-smmu-fence-test.exe')
    run(ROOT/'out/marble-smmu-fence-test.exe')
    run(clang, '-O2', 'tools/tests/marble-gmu-preflight.c', '-o', 'out/marble-gmu-preflight-test.exe')
    run(ROOT/'out/marble-gmu-preflight-test.exe')
    run(sys.executable, 'tools/marble-image.py', '--source', a.source.resolve(),
        '--loader', 'out/loader-marble.efi', '--lzma-encoder', 'out/marble-lzma.exe',
        '--output', 'out/marble-userspace-boot.img')
    run(sys.executable, 'tools/marble-host-tests.py', '--python-only')

if __name__ == '__main__':
    main()
