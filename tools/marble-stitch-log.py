"""Join an unwrapped live prefix and final log at one identical complete line.

Keeps both raw sources and records exact offsets/hashes; never synthesizes,
deduplicates, reorders, or changes a hardware evidence line.
"""
import argparse,hashlib,json
from pathlib import Path
def digest(raw):return hashlib.sha256(raw).hexdigest()
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--prefix',type=Path,required=True)
 p.add_argument('--suffix',type=Path,required=True);p.add_argument('--anchor',required=True)
 p.add_argument('--output',type=Path,required=True);a=p.parse_args()
 left=a.prefix.read_bytes();right=a.suffix.read_bytes();anchor=a.anchor.encode()+b'\n'
 if len(left)>=1280*1024 or b'log between the first 256 KB' in left:raise ValueError('prefix may have wrapped')
 needle=b'\n'+anchor
 if (b'\n'+left).count(needle)!=1 or (b'\n'+right).count(needle)!=1:raise ValueError('anchor must be one exact full line in each raw log')
 x=(b'\n'+left).index(needle);y=(b'\n'+right).index(needle)
 live_marker=b'== KERNEL LOG (the kernel is still running) ==\n'
 final_marker=b'== KERNEL LOG ==\n'
 if left.count(live_marker)!=1 or right.count(final_marker)!=1:raise ValueError('loader log headers differ')
 lstart=left.index(live_marker)+len(live_marker);rstart=right.index(final_marker)+len(final_marker)
 common=left[:left.index(b'\n\n== KERNEL LOG')]
 if not right.startswith(common) or x<lstart or y<rstart:raise ValueError('raw logs are not the same boot')
 joined=right[:rstart]+left[lstart:x]+right[y:]
 a.output.write_bytes(joined)
 meta=dict(operation='exact-prefix-suffix-at-identical-line',anchor=a.anchor,
  prefix=dict(path=str(a.prefix.resolve()),sha256=digest(left),offset=lstart,bytes=x-lstart),
  suffix=dict(path=str(a.suffix.resolve()),sha256=digest(right),header_bytes=rstart,offset=y),
  output=dict(path=str(a.output.resolve()),sha256=digest(joined)))
 a.output.with_suffix('.provenance.json').write_text(json.dumps(meta,indent=2)+'\n')
 print('Joined',len(joined),'bytes with raw-source provenance; original logs unchanged')
if __name__=='__main__':main()
