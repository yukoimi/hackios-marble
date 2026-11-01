"""Make a tiny RAM-only APFS for the static thermal-idle program, not iOS."""
import argparse, hashlib, json, stat, sys
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--program',type=Path,default=ROOT/'out/linux-build/marble-thermal-idle')
    p.add_argument('--out',type=Path,default=ROOT/'out/marble-thermal-idle-minimal.apfs')
    a=p.parse_args();raw=a.program.read_bytes()
    if raw[:8]!=bytes.fromhex('cffaedfe0c000001'):
        raise ValueError('expected an arm64 Mach-O')
    sys.path.insert(0,str(ROOT/'tools/apfs'));import mkapfs as apfs
    tree=apfs.Node('',stat.S_IFDIR|0o755)
    for name in ('sbin','dev','private','redwood'):
        tree.children[name]=apfs.Node(name,stat.S_IFDIR|0o755)
    tree.children['private'].children['var']=apfs.Node('var',stat.S_IFDIR|0o755)
    tree.children['sbin'].children['launchd']=apfs.Node('launchd',stat.S_IFREG|0o755,source=str(a.program.resolve()))
    with open(str(a.out)+'.spool','w+b') as spool:
        volume=apfs.Volume(0,'thermal-idle',tree,True,1683900000*1000000000,spool,[])
        container=apfs.Container([volume],256,a.out.name);container.layout();container.write(str(a.out))
    Path(str(a.out)+'.spool').unlink()
    info=dict(scope='static sleeping init only; no iOS/GPU; RAM APFS',
              program_sha256=hashlib.sha256(raw).hexdigest(),
              image_sha256=hashlib.sha256(a.out.read_bytes()).hexdigest(),bytes=a.out.stat().st_size)
    a.out.with_suffix('.json').write_text(json.dumps(info,indent=2)+'\n')
    print(json.dumps(info,indent=2))


if __name__=='__main__':main()
