"""Sanitize the actual display functions on the private Linux VM; no phone access."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import shlex
import shutil

ROOT=Path(__file__).resolve().parents[1]

def extract(text,start,end='\n}\n'):
    at=text.index(start)
    return text[at:text.index(end,at)+len(end)]

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--out',type=Path,required=True)
    p.add_argument('--build-kernel',action='store_true')
    a=p.parse_args();out=a.out.resolve()
    if out.exists() or not out.is_relative_to(ROOT/'out'):p.error('new directory below out required')
    out.mkdir(parents=True)
    names=['xnu/files/iokit/Kernel/MiniIOSurface.cpp','xnu/files/iokit/Kernel/MiniIOMobileFramebuffer.cpp',
           'tools/tests/marble-display-bounds.cpp','tools/marble-linux-build.sh','tools/marble_source.py']
    for name in names:
        dest=out/'source'/name;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(ROOT/name,dest)
    text=(ROOT/names[1]).read_text(encoding='utf-8')
    include='\n'.join(extract(text,start,end) for start,end in [
        ('static bool\nsurfaceHolds(','\n}\n'),
        ('static inline uint32_t\nblend(','\n}\n'),
        ('static inline int64_t\nsample_at(','\n}\n'),
        ('#define DST_LIMIT','\n};'),
        ('void\nIOMobileFramebuffer::drawRows(','\n}\n'),
        ('void\nIOMobileFramebuffer::drawLayer(','\n}\n')])
    (out/'display-production.inc').write_text(include,encoding='utf-8',newline='\n')
    spec=importlib.util.spec_from_file_location('display_vm',ROOT/'tools/marble-build-vm.py')
    vm=importlib.util.module_from_spec(spec);spec.loader.exec_module(vm);c=vm.connect()
    guest='/home/builder/marble';relative=out.relative_to(ROOT).as_posix();build=guest+'/'+relative
    def run(name,command):
        (out/(name+'.command')).write_text(command+'\n',encoding='utf-8')
        _,s,_=c.exec_command(command);s.channel.set_combine_stderr(True);data=s.read();rc=s.channel.recv_exit_status()
        (out/(name+'.log')).write_bytes(data);print(name,rc,data.decode(errors='replace')[-1800:],flush=True)
        if rc:raise RuntimeError(name+' failed')
    try:
        run('prepare','mkdir -p '+shlex.quote(build))
        with c.open_sftp() as s:
            s.put(str(out/'display-production.inc'),build+'/display-production.inc')
            s.put(str(ROOT/names[2]),build+'/test.cpp')
        run('bounds','clang++ -std=c++17 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined '
            '-fno-omit-frame-pointer '+shlex.quote(build+'/test.cpp')+' -o '+shlex.quote(build+'/bounds')+
            ' && '+shlex.quote(build+'/bounds'))
        (out/'test-result.json').write_text(json.dumps(dict(passed=True,
            sanitizers=['address','undefined'],source='production bodies extracted without edits',
            hardware_validated=False,scope='CPU composition pixels and allocation lifecycle; not GPU or IOKit scheduling'),indent=2)+'\n')
        if a.build_kernel:
            with c.open_sftp() as s:
                for name in names[:2]:
                    s.put(str(ROOT/name),guest+'/'+name)
                    s.put(str(ROOT/name),guest+'/xnu-src/xnu-8796.121.2/'+name[len('xnu/files/'):])
            script=(ROOT/'tools/marble-linux-build.sh').read_text().replace('out/linux-build',relative+'/kernel')
            (out/'kernel-build.sh').write_text(script,encoding='utf-8',newline='\n')
            kernel_script=guest+'/tools/'+out.name+'-kernel.sh'
            with c.open_sftp() as s:s.put(str(out/'kernel-build.sh'),kernel_script)
            run('kernel','cd '+shlex.quote(guest)+' && bash '+shlex.quote(kernel_script)+' kernel')
            with c.open_sftp() as s:
                for name in ['mach.marble-source','mach.json','kernel.log','environment.txt']:
                    s.get(build+'/kernel/'+name,str(out/name))
            profile=json.loads((out/'mach.json').read_text())
            profile.update(requires_cpu_cap=True,gpu_preview_kernel_abi='display-counter-v2',
                display_backend='existing DSI blank-window staging copy plus opt-in sequence-bound GLES final scale',
                hardware_status='candidate; not hardware validated',
                build_limits='compile/link successful; inherited export checks remain; inspect kernel.log',
                reused_upstream_commit='7867dc140043cdc3151886d67735b3894414e7bf',
                display_bounds_fixes=['G2','G5','G7','G10'])
            (out/'mach.json').write_text(json.dumps(profile,indent=2)+'\n')
            spec=importlib.util.spec_from_file_location('display_source',ROOT/'tools/marble_source.py')
            module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
            module.verify_kernel((out/'mach.marble-source').read_bytes(),profile)
        manifest={str(x.relative_to(out)):hashlib.sha256(x.read_bytes()).hexdigest() for x in out.rglob('*') if x.is_file()}
        (out/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    finally:c.close()

if __name__=='__main__':main()
