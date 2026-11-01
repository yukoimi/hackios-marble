"""Build an isolated EAGL candidate against the existing guest Mesa; no phone access."""
import argparse, hashlib, importlib.util, json, shlex, shutil, tarfile
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--out',type=Path,required=True)
    p.add_argument('--base-programs',type=Path,default=Path('out/linux-build/userland-brightness-backend-v3'))
    a=p.parse_args();target=a.out.resolve()
    if not target.is_relative_to(ROOT/'out') or target.exists():
        p.error('new output directory must be inside out')
    target.mkdir(parents=True)
    shutil.copytree(a.base_programs,target/'programs')
    spec=importlib.util.spec_from_file_location('build_vm',ROOT/'tools/marble-build-vm.py')
    vm=importlib.util.module_from_spec(spec);spec.loader.exec_module(vm)
    client=vm.connect()
    source_names=['userland/src/eagl.c','userland/src/gl_osmesa.c','userland/src/redwood_gl.h',
                  'userland/src/eagl_slots.txt','userland/mkeagl.py']
    guest='/home/builder/marble';build=guest+'/out/'+target.name
    def run(command):
        _,stdout,_=client.exec_command(command);stdout.channel.set_combine_stderr(True)
        data=stdout.read();(target/'build.log').write_bytes(data)
        if stdout.channel.recv_exit_status():raise RuntimeError(data.decode(errors='replace'))
    try:
        run('mkdir '+shlex.quote(build))
        with client.open_sftp() as sftp:
            for name in source_names:
                src=ROOT/name;dst=target/'source'/name;dst.parent.mkdir(parents=True,exist_ok=True)
                shutil.copyfile(src,dst);sftp.put(str(src),guest+'/'+name)
        command=('cd '+shlex.quote(guest)+' && source xnu-env.sh && '+
            'python3 userland/mkeagl.py userland/src/eagl_slots.txt '+
            'xnu-deps/build/mesa/out/gl.xml xnu-deps/build/mesa/out/exported-gl.txt userland/src/eagl.c > '+
            shlex.quote(build+'/eagl_gen.h')+' && xnu-clang -arch arm64 '+
            '-isysroot /home/builder/theos/sdks/iPhoneOS16.5.sdk -O1 -dynamiclib '+
            '-install_name /redwood/lib/libredwood-gl.dylib -I'+shlex.quote(build)+
            ' -framework CoreFoundation -framework IOKit -framework IOSurface -lobjc '+
            'xnu-deps/build/mesa/out/libOSMesa.8.dylib -o '+shlex.quote(build+'/libredwood-gl.dylib')+
            ' userland/src/eagl.c userland/src/gl_osmesa.c')
        (target/'command.txt').write_text(command+'\n')
        run(command)
        with client.open_sftp() as sftp:
            sftp.get(build+'/libredwood-gl.dylib',str(target/'programs/libredwood-gl.dylib'))
            sftp.get(build+'/eagl_gen.h',str(target/'source/eagl_gen.h'))
            sftp.get(guest+'/xnu-deps/build/mesa/out/libOSMesa.8.dylib',str(target/'linked-mesa.dylib'))
        # Refuse an ABI experiment that accidentally pairs a newly linked Mesa
        # with a different runtime library in the inherited support disk.
        if digest(target/'linked-mesa.dylib')!=digest(target/'programs/libOSMesa.8.dylib'):
            raise RuntimeError('linked and runtime Mesa differ; candidate is incomplete')
        manifest={str(x.relative_to(target)):dict(bytes=x.stat().st_size,sha256=digest(x))
                  for x in sorted(target.rglob('*')) if x.is_file()}
        (target/'manifest.json').write_text(json.dumps(dict(status='candidate; requires hardware tests',files=manifest),indent=2)+'\n')
        with tarfile.open(target/'source.tar.gz','w:gz') as archive:
            archive.add(target/'source',arcname='source')
    finally:
        client.close()
    print(target)

if __name__=='__main__':main()
