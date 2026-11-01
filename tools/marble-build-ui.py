"""Build isolated UI diagnostics against the existing iOS16.5 VM SDK."""
import argparse,hashlib,importlib.util,json,shlex,shutil,subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--out',type=Path,required=True)
    p.add_argument('--base-programs',type=Path,default=Path('out/gpu-scale-v4-layout-plan/programs'))
    p.add_argument('--media',action='store_true',help='also rebuild upstream mediaserverd compatibility from retained source')
    p.add_argument('--control-input',action='store_true',help='also build the marker-gated native input classification consumer with its boundary checks')
    a=p.parse_args();out=a.out.resolve()
    if not out.is_relative_to(ROOT/'out') or out.exists():p.error('new directory under out required')
    out.mkdir();shutil.copytree(a.base_programs,out/'programs')
    if a.control_input:
        llvm=ROOT.parent/'toolchains/llvm-mingw-20261020-ucrt-x86_64/bin/clang.exe'
        run=subprocess.run([str(llvm),'-O2','-Wall','-Wextra','tools/tests/marble-control-input.c','-o',str(out/'control-input-test.exe')],cwd=ROOT,capture_output=True)
        (out/'control-input-build.log').write_bytes(run.stdout+run.stderr)
        if run.returncode:raise RuntimeError('control input host boundary build failed')
        run=subprocess.run([str(out/'control-input-test.exe')],capture_output=True)
        (out/'control-input-test.log').write_bytes(run.stdout+run.stderr)
        if run.returncode:raise RuntimeError('control input host boundary checks failed')
    spec=importlib.util.spec_from_file_location('vm_ui',ROOT/'tools/marble-build-vm.py')
    vm=importlib.util.module_from_spec(spec);spec.loader.exec_module(vm);client=vm.connect()
    guest='/home/builder/marble';build=guest+'/'+out.relative_to(ROOT).as_posix()
    try:
        _,stdout,_=client.exec_command('mkdir -p '+shlex.quote(build));assert stdout.channel.recv_exit_status()==0
        with client.open_sftp() as s:
            s.put(str(ROOT/'tools/marble-ui-layout.c'),guest+'/tools/marble-ui-layout.c')
            s.put(str(ROOT/'tools/marble-wallpaper.c'),guest+'/tools/marble-wallpaper.c')
            s.put(str(ROOT/'userland/src/rwtouch.c'),guest+'/userland/src/rwtouch.c')
            s.put(str(ROOT/'userland/src/redwood_input.h'),guest+'/userland/src/redwood_input.h')
            s.put(str(ROOT/'userland/src/marble_control_input.h'),guest+'/userland/src/marble_control_input.h')
        cmd='cd '+guest+' && source xnu-env.sh && xnu-clang -arch arm64 -isysroot /home/builder/theos/sdks/iPhoneOS16.5.sdk -O1 -Wall -Wextra -fblocks -dynamiclib -framework CoreFoundation -framework CoreGraphics -framework UIKit -lobjc -install_name /redwood/lib/marble-ui-layout.dylib tools/marble-ui-layout.c -o '+build+'/marble-ui-layout.dylib'
        (out/'command.txt').write_text(cmd+'\n');_,stdout,_=client.exec_command(cmd);stdout.channel.set_combine_stderr(True)
        log=stdout.read();(out/'build.log').write_bytes(log)
        if stdout.channel.recv_exit_status():raise RuntimeError(log.decode(errors='replace'))
        helper='cd '+guest+' && source xnu-env.sh && xnu-clang -arch arm64 -isysroot /home/builder/theos/sdks/iPhoneOS16.5.sdk -O1 -Wall -Wextra -fblocks -framework CoreFoundation -framework UIKit -lobjc tools/marble-wallpaper.c -o '+build+'/marble-wallpaper'
        (out/'wallpaper-command.txt').write_text(helper+'\n');_,stdout,_=client.exec_command(helper);stdout.channel.set_combine_stderr(True)
        helper_log=stdout.read();(out/'wallpaper-build.log').write_bytes(helper_log)
        if stdout.channel.recv_exit_status():raise RuntimeError(helper_log.decode(errors='replace'))
        with client.open_sftp() as s:s.get(build+'/marble-ui-layout.dylib',str(out/'programs/marble-ui-layout.dylib'))
        with client.open_sftp() as s:s.get(build+'/marble-wallpaper',str(out/'programs/marble-wallpaper'))
        command='cd '+guest+' && source xnu-env.sh && xnu-clang -arch arm64 -isysroot /home/builder/theos/sdks/iPhoneOS16.5.sdk -O2 -Wall -Wextra -framework CoreFoundation userland/src/rwtouch.c -o '+build+'/rwtouch'
        (out/'rwtouch-command.txt').write_text(command+'\n');_,stdout,_=client.exec_command(command);stdout.channel.set_combine_stderr(True)
        log=stdout.read();(out/'rwtouch-build.log').write_bytes(log)
        if stdout.channel.recv_exit_status():raise RuntimeError(log.decode(errors='replace'))
        with client.open_sftp() as s:s.get(build+'/rwtouch',str(out/'programs/rwtouch'))
        for name in ('rwtouch.c','redwood_input.h'):shutil.copy2(ROOT/'userland/src'/name,out/name)
        shutil.copy2(ROOT/'userland/src/marble_control_input.h',out/'marble_control_input.h')
        shutil.copy2(ROOT/'tools/marble-ui-layout.c',out/'marble-ui-layout.c')
        shutil.copy2(ROOT/'tools/marble-wallpaper.c',out/'marble-wallpaper.c')
        if a.control_input:
            sources=['userland/src/inject.c','userland/src/redwood_gl.h','userland/src/redwood_input.h','userland/src/marble_control_input.h','tools/tests/marble-control-input.c']
            with client.open_sftp() as s:
                for name in sources:
                    if name.startswith('userland/'):s.put(str(ROOT/name),guest+'/'+name)
                    dst=out/'source'/name;dst.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(ROOT/name,dst)
            command='cd '+guest+' && source xnu-env.sh && xnu-clang -arch arm64 -isysroot /home/builder/theos/sdks/iPhoneOS16.5.sdk -O1 -Wall -Wextra -dynamiclib -install_name /redwood/lib/libredwood.dylib -framework CoreFoundation -framework IOKit -lobjc userland/src/inject.c -o '+build+'/libredwood.dylib'
            (out/'input-command.txt').write_text(command+'\n');_,stdout,_=client.exec_command(command);stdout.channel.set_combine_stderr(True)
            log=stdout.read();(out/'input-build.log').write_bytes(log)
            if stdout.channel.recv_exit_status():raise RuntimeError(log.decode(errors='replace'))
            with client.open_sftp() as s:s.get(build+'/libredwood.dylib',str(out/'programs/libredwood.dylib'))
        if a.media:
            sources=['userland/src/inject_media.c','userland/src/camera_device.c','userland/src/camera_isp.c']
            with client.open_sftp() as s:
                for name in sources:
                    s.put(str(ROOT/name),guest+'/'+name)
                    dst=out/'source'/name;dst.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(ROOT/name,dst)
            command='cd '+guest+' && source xnu-env.sh && xnu-clang -arch arm64 -isysroot /home/builder/theos/sdks/iPhoneOS16.5.sdk -O2 -dynamiclib -install_name /redwood/lib/libredwood-media.dylib -framework CoreFoundation -framework CoreMedia -framework CoreVideo -framework IOSurface -lobjc '+' '.join(sources)+' -o '+build+'/libredwood-media.dylib'
            (out/'media-command.txt').write_text(command+'\n');_,stdout,_=client.exec_command(command);stdout.channel.set_combine_stderr(True)
            log=stdout.read();(out/'media-build.log').write_bytes(log)
            if stdout.channel.recv_exit_status():raise RuntimeError(log.decode(errors='replace'))
            with client.open_sftp() as s:s.get(build+'/libredwood-media.dylib',str(out/'programs/libredwood-media.dylib'))
        (out/'manifest.json').write_text(json.dumps({str(f.relative_to(out)):hashlib.sha256(f.read_bytes()).hexdigest() for f in out.rglob('*') if f.is_file()},indent=2)+'\n')
    finally:client.close()
    print(out)
if __name__=='__main__':main()
