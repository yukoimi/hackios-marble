"""Extract the production EAGL copy/shader code for a host Mesa GLES pixel test.

No phone access. Run the generated C with a surfaceless software EGL context.
"""
import argparse, hashlib, importlib.util, json, re, shlex, shutil
from pathlib import Path


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--source',type=Path,default=Path('userland/src/eagl.c'))
    p.add_argument('--out',type=Path,default=Path('out/backdrop-host-test'))
    p.add_argument('--run-vm',action='store_true',help='compile and execute the extracted code on guest llvmpipe with ASan/UBSan; no phone access')
    a=p.parse_args();raw=a.source.read_bytes();text=raw.decode().replace('\r\n','\n');a.out.mkdir(parents=True,exist_ok=True)
    pieces=['/* Extracted production code; source SHA256 '+hashlib.sha256(raw).hexdigest()+' */\n']
    for name in ('plain_vertex','vibrant_fragment','group_over_fragment','group_flat_fragment',
                 'fragment_start','fragment_fetch_copy','fragment_fetch_copy_end'):
        match=re.search(r'static const char '+name+r'\[\] =\n.*?;\n',text,re.S)
        if not match:raise ValueError('missing shader '+name)
        pieces.append(match[0])
    for signature in ('static GLuint\nplain_program(', 'static bool\nbackdrop_begin(', 'static void\nbackdrop_end('):
        start=text.index(signature);end=text.index('\n}\n',start)+3
        pieces.append(text[start:end])
    (a.out/'marble-backdrop-production.h').write_text('\n'.join(pieces),encoding='utf8')
    print(a.out/'marble-backdrop-production.h')
    if a.run_vm:
        root=Path(__file__).resolve().parents[1]
        directory=a.out.resolve()
        if not directory.is_relative_to(root/'out'):
            raise ValueError('VM test output must be inside out')
        shutil.copyfile(root/'tools/tests/marble-backdrop-egl.c',directory/'marble-backdrop-egl.c')
        spec=importlib.util.spec_from_file_location('backdrop_build_vm',root/'tools/marble-build-vm.py')
        vm=importlib.util.module_from_spec(spec);spec.loader.exec_module(vm)
        guest='/home/builder/marble/'+directory.relative_to(root).as_posix()
        client=vm.connect()
        try:
            def run(command):
                _,stdout,_=client.exec_command(command);stdout.channel.set_combine_stderr(True)
                output=stdout.read();return stdout.channel.recv_exit_status(),output
            rc,output=run('mkdir -p '+shlex.quote(guest))
            if rc:raise RuntimeError(output.decode(errors='replace'))
            with client.open_sftp() as sftp:
                for name in ('marble-backdrop-production.h','marble-backdrop-egl.c'):
                    sftp.put(str(directory/name),guest+'/'+name)
            command=('cd '+shlex.quote(guest)+' && clang -g -O1 -fsanitize=address,undefined '
                '-fno-omit-frame-pointer marble-backdrop-egl.c -o backdrop-test -lEGL -lGLESv2 -lm && '
                'EGL_PLATFORM=surfaceless LIBGL_ALWAYS_SOFTWARE=1 ASAN_OPTIONS=detect_leaks=0 ./backdrop-test')
            (directory/'command.txt').write_text(command+'\n')
            rc,output=run(command);(directory/'run.log').write_bytes(output)
            print(output.decode(errors='replace'))
            match=re.search(rb'PASS cases=(\d+) channels=(\d+) GL_errors=(\d+)',output)
            result=dict(passed=rc==0 and match is not None,exit_code=rc,
                source_sha256=hashlib.sha256(raw).hexdigest(),
                sanitizers=['address','undefined'],leak_check=False,
                scope='production helper and shader preambles on host llvmpipe; not Adreno or optical scanout')
            if match:result.update(cases=int(match[1]),channels=int(match[2]),gl_errors=int(match[3]))
            (directory/'result.json').write_text(json.dumps(result,indent=2)+'\n')
            if not result['passed']:raise RuntimeError('host backdrop test failed; see run.log')
        finally:client.close()


if __name__=='__main__':main()
