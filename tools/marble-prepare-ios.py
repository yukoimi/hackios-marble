#!/usr/bin/env python3
"""Prepare Apple's iOS 16.5 inputs with native Windows 7zip and unzbm.

Use --reuse-extracted to validate existing extraction without extracting again.
Requires NTFS alternate streams for 7zip's otherwise unsupported LZBITMAP files.
No device access; no native Unix symlink privilege is needed.
"""
import argparse
import importlib.util
import os
from pathlib import Path
import stat
import subprocess
import sys
from marble_windows import extracted_path

ROOT=Path(__file__).resolve().parents[1]

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--sevenzip',type=Path,required=True)
    p.add_argument('--unzbm',type=Path,required=True)
    p.add_argument('--reuse-extracted',action='store_true')
    a=p.parse_args()
    z=str(a.sevenzip.resolve()); unzbm=str(a.unzbm.resolve())
    os.chdir(ROOT); ios=ROOT/'ios165'; ios.mkdir(exist_ok=True)
    out=ROOT/'out'; out.mkdir(exist_ok=True)
    spec=importlib.util.spec_from_file_location('select_system',ios/'select-system.py')
    select=importlib.util.module_from_spec(spec); spec.loader.exec_module(select)
    inputs=[('rootfs','096-04088-077.dmg',4728546953),
            ('cryptex-os','096-04666-078.dmg',2726297600),
            ('cryptex-app','096-03536-083.dmg',10485760)]
    for name,member,size in inputs:
        image=ios/(name+'.dmg')
        if not image.exists(): image=ios/(name+'.dmg.part')
        if not image.exists() or image.stat().st_size!=size:
            subprocess.run([sys.executable,'ios165/fetch-from-ipsw.py',member,str(image)],check=True)
        if image.stat().st_size!=size: raise RuntimeError('incomplete download '+name)
        listing=subprocess.run([z,'l','-slt','-sccUTF-8',str(image)],capture_output=True,check=True).stdout.decode('utf8')
        listing=listing.replace('\r\n','\n').replace('\\','/')
        slt=ios/(name+'.slt'); slt.write_text(listing,encoding='utf8',newline='\n')
        if name=='rootfs':
            subprocess.run([sys.executable,'-X','utf8','ios165/select-system.py',str(slt),
                            'ios165/system.files','ios165/system.manifest'],check=True)
            dest=ios/'rootfs'; options=['-snc','@ios165/system.files']
        else:
            dest=ios/('cryptex' if name=='cryptex-os' else name)
            options=['-sns']+(['-xr!*.symbols*','-x!System/DriverKit'] if name=='cryptex-os' else [])
        if not a.reuse_extracted:
            with open(out/('extract-'+name+'.log'),'wb') as log:
                # Expected: 7zip reports unsupported ZBM compression and unprivileged symlinks.
                # Content and all required regular file sizes are checked below after decoding.
                subprocess.run([z,'x','-y','-o'+str(dest),str(image),*options],stdout=log,stderr=log)
        files=[]
        for f in dest.rglob('*'):
            if f.is_file() and Path(str(f)+':com.apple.decmpfs').exists():
                files.append(('\\\\?\\' if os.name=='nt' else '')+str(f.resolve()))
        for i in range(0,len(files),15): subprocess.run([unzbm,*files[i:i+15]],check=True)
        metadata={e['Path']:e for e in select.entries(str(slt))}
        if name=='rootfs':
            paths=(ios/'system.files').read_text(encoding='utf8').splitlines()
        else:
            paths=[path for path,e in metadata.items() if stat.S_ISREG(select.mode_bits(e['Mode']) or 0)
                   and e.get('Alternate Stream')!='+' and
                   not (name=='cryptex-os' and (path.startswith('System/DriverKit/') or '.symbols' in path))]
        for path in paths:
            f=extracted_path(dest,path); expected=int(metadata[path].get('Size') or 0)
            if not f.is_file() or f.stat().st_size!=expected:
                raise RuntimeError(f'incomplete extraction: {f}, expected {expected} bytes')
        print(f'{name}: {len(paths)} regular files verified',flush=True)
    print('Ready: python -X utf8 tools/marble-userspace.py')

if __name__=='__main__': main()
