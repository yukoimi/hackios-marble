"""Build/test an isolated GLES scale candidate on the private Linux VM.

No phone access. Outputs and source snapshots are kept under a new out folder.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import shlex
import shutil

ROOT=Path(__file__).resolve().parents[1]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--out',type=Path,required=True)
    p.add_argument('--base-programs',type=Path,default=Path('out/eagl-copy-timing-v2-plan/programs'))
    p.add_argument('--helper-only',action='store_true',help='build/test the userspace helper without changing or rebuilding the kernel')
    p.add_argument('--ring-context',choices=('default','shared-vm-experiment'),default='default',help='isolated kernel experiment: rely on Mesa per-command-stream restore in the shared GPU address space')
    a=p.parse_args();target=a.out.resolve()
    if a.helper_only and a.ring_context!='default':p.error('ring experiment requires a new kernel build')
    if not target.is_relative_to(ROOT/'out') or target.exists():p.error('output must be a new directory inside out')
    target.mkdir(parents=True)
    shutil.copytree(a.base_programs,target/'programs')
    spec=importlib.util.spec_from_file_location('scale_build_vm',ROOT/'tools/marble-build-vm.py')
    vm=importlib.util.module_from_spec(spec);spec.loader.exec_module(vm)
    client=vm.connect();guest='/home/builder/marble';relative=target.relative_to(ROOT).as_posix();build=guest+'/'+relative
    names=['tools/marble-gpu-scale.c','tools/marble-scale-blit.h','tools/tests/marble-scale-pixels.h',
        'tools/tests/marble-scale-state.c','tools/tests/marble-scale-egl.c','tools/tests/marble-scale-kernel.cpp',
        'userland/src/gl_osmesa.c','userland/src/redwood_gl.h',
        'xnu/files/iokit/Kernel/RedwoodGPU.cpp','xnu/files/iokit/Kernel/MarbleGPUScale.inc',
        'xnu/files/iokit/Kernel/MiniIOMobileFramebuffer.cpp','xnu/files/bsd/sys/marble_gpu_scale.h',
        'tools/marble-linux-build.sh','tools/marble_source.py','tools/marble-userspace.py']
    ring_name='xnu/files/iokit/Kernel/MarbleGPURing.inc'
    if a.ring_context!='default':names.append(ring_name)
    def run(name,command):
        print(name,flush=True);(target/(name+'.command')).write_text(command+'\n',encoding='utf8')
        _,stdout,_=client.exec_command(command);stdout.channel.set_combine_stderr(True)
        data=stdout.read();code=stdout.channel.recv_exit_status();(target/(name+'.log')).write_bytes(data)
        print(data.decode(errors='replace')[-4500:],flush=True)
        if code:raise RuntimeError(name+' failed; see log')
    try:
        run('prepare','mkdir -p '+shlex.quote(build))
        with client.open_sftp() as sftp:
            for name in names:
                src=ROOT/name;dst=target/'source'/name;dst.parent.mkdir(parents=True,exist_ok=True)
                shutil.copyfile(src,dst)
                if name==ring_name and a.ring_context=='shared-vm-experiment':
                    old='bool reset=!marbleSelectedClient || marbleSelectedClient!=marbleLastClient ||\n        marbleSelectedQueue!=marbleLastQueue;'
                    new='bool reset=!marbleLastClient; /* isolated shared-global-VA experiment; Mesa restores each command stream */'
                    content=src.read_text()
                    if content.count(old)!=1:raise RuntimeError('qualified ring selection expression changed')
                    dst.write_text(content.replace(old,new),encoding='utf8',newline='\n')
                sftp.put(str(dst),guest+'/'+name)
                if name.startswith('xnu/files/'):
                    # Incremental builds use the already patched source tree.
                    sftp.put(str(dst),guest+'/xnu-src/xnu-8796.121.2/'+name[len('xnu/files/'):])
        sanitizer='clang -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer '
        command=('cd '+shlex.quote(guest)+' && '+sanitizer+'tools/tests/marble-scale-state.c -o '+
            shlex.quote(build+'/state-test')+' && '+shlex.quote(build+'/state-test')+' && '+
            'clang++ -std=c++17 -pthread -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer '+
            'tools/tests/marble-scale-kernel.cpp -o '+shlex.quote(build+'/kernel-test')+' && '+shlex.quote(build+'/kernel-test')+' && '+sanitizer+
            'tools/tests/marble-scale-egl.c -o '+shlex.quote(build+'/egl-test')+' -lEGL -lGLESv2 && '+
            'EGL_PLATFORM=surfaceless LIBGL_ALWAYS_SOFTWARE=1 ASAN_OPTIONS=detect_leaks=0 '+shlex.quote(build+'/egl-test'))
        run('host-tests',command)
        (target/'host-result.json').write_text(json.dumps(dict(passed=True,sanitizers=['address','undefined'],
            scope='shared production blit on host llvmpipe and ABI lifecycle; not FD725 or display qualification'),indent=2)+'\n')
        command=('cd '+shlex.quote(guest)+' && source xnu-env.sh && xnu-clang -arch arm64 '+
            '-isysroot /home/builder/theos/sdks/iPhoneOS16.5.sdk -O2 -Wall -Wextra '+
            '-Wno-deprecated-declarations -framework CoreFoundation -framework IOKit -framework IOSurface '+
            'tools/marble-gpu-scale.c userland/src/gl_osmesa.c xnu-deps/build/mesa/out/libOSMesa.8.dylib '+
            '-o '+shlex.quote(build+'/marble-gpu-scale')+' && '+
            '/home/builder/theos/toolchain/linux/iphone/bin/otool -L '+shlex.quote(build+'/marble-gpu-scale'))
        run('helper-build',command)
        with client.open_sftp() as sftp:
            sftp.get(build+'/marble-gpu-scale',str(target/'programs/marble-gpu-scale'))
            sftp.get(guest+'/xnu-deps/build/mesa/out/libOSMesa.8.dylib',str(target/'linked-mesa.dylib'))
        if digest(target/'linked-mesa.dylib')!=digest(target/'programs/libOSMesa.8.dylib'):
            raise RuntimeError('linked/runtime Mesa mismatch')
        if a.helper_only:
            manifest={str(f.relative_to(target)):dict(bytes=f.stat().st_size,sha256=digest(f))
                for f in sorted(target.rglob('*')) if f.is_file()}
            (target/'manifest.json').write_text(json.dumps(dict(status='userspace helper candidate; not hardware validated; kernel unchanged',files=manifest),indent=2)+'\n')
            print(target,flush=True);return
        # Reuse the established build commands with a separate output/log
        # directory. Existing baseline kernel artifacts are not overwritten.
        script=(ROOT/'tools/marble-linux-build.sh').read_text().replace('out/linux-build',relative+'/kernel')
        (target/'kernel-build.sh').write_text(script,encoding='utf8',newline='\n')
        with client.open_sftp() as sftp:sftp.put(str(target/'kernel-build.sh'),guest+'/tools/'+target.name+'-kernel.sh')
        run('kernel-build','cd '+shlex.quote(guest)+' && bash '+shlex.quote('tools/'+target.name+'-kernel.sh')+' kernel')
        with client.open_sftp() as sftp:
            for name in ('mach.marble-source','mach.json','kernel.log','environment.txt'):
                # pathlib.with_suffix on mach.marble-source yields mach.json.
                sftp.get(build+'/kernel/'+name,str(target/name))
        kernel=target/'mach.marble-source';profile=json.loads((target/'mach.json').read_text())
        profile.update(requires_cpu_cap=True,gpu_preview_kernel_abi='display-counter-v2',
            display_backend='existing DSI blank-window staging copy plus opt-in sequence-bound GLES final scale',
            hardware_status='candidate; not hardware validated',
            build_limits='compile/link successful; inspect kernel.log for inherited export-check failures')
        profile['ring_context_strategy']=a.ring_context
        (target/'mach.json').write_text(json.dumps(profile,indent=2)+'\n')
        src=importlib.util.spec_from_file_location('scale_source',ROOT/'tools/marble_source.py')
        check=importlib.util.module_from_spec(src);src.loader.exec_module(check);check.verify_kernel(kernel.read_bytes(),profile)
        if b'MSCALE-MARBLE-GLES-v1' not in kernel.read_bytes():raise RuntimeError('scale code absent from kernel')
        manifest={str(f.relative_to(target)):dict(bytes=f.stat().st_size,sha256=digest(f))
            for f in sorted(target.rglob('*')) if f.is_file()}
        (target/'manifest.json').write_text(json.dumps(dict(status='candidate; hardware tests required',files=manifest),indent=2)+'\n')
    finally:
        if a.ring_context!='default':
            with client.open_sftp() as sftp:
                sftp.put(str(ROOT/ring_name),guest+'/'+ring_name)
                sftp.put(str(ROOT/ring_name),guest+'/xnu-src/xnu-8796.121.2/'+ring_name[len('xnu/files/'):])
        client.close()
    print(target,flush=True)


if __name__=='__main__':main()
