"""Pack only two pinned local ADSP inputs into a bounded RAM-upload bundle."""
import argparse,hashlib,json,struct
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def make(out):
 inputs=[(ROOT/'out/pil-header-only-v3-reloc-verified-plan/PILDxe-noauto.efi',118784,'cc310ec13454b595bfc3d5302e41de9eb5eecc97cb490412db47cca6d5b2fc44',4096),
  (ROOT/'out/pil-ram-files-v1-plan/firmware-files.bin',30057264,'2848fc433f124550c95d72e5de2697858d68be3070ddf2b5d8816448eca9dc19',122880)]
 raw=bytearray(30180144);struct.pack_into('<8Q',raw,0,0x313030505344414d,1,len(raw),4096,118784,122880,30057264,0);manifest=[]
 for path,size,sha,offset in inputs:
  data=path.read_bytes()
  if len(data)!=size or hashlib.sha256(data).hexdigest()!=sha:raise ValueError('pinned input differs: '+path.name)
  raw[offset:offset+size]=data;manifest.append(dict(path=str(path.relative_to(ROOT)),bytes=size,sha256=sha,offset=offset))
 out.mkdir(exist_ok=False);(out/'dependencies.bin').write_bytes(raw)
 (out/'manifest.json').write_text(json.dumps(dict(format='MADSP001',bytes=len(raw),sha256=hashlib.sha256(raw).hexdigest(),inputs=manifest,
  scope='offline RAM bundle only; does not enable DSP commands or write a phone partition'),indent=2)+'\n')
 return out
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--out',type=Path,required=True);a=p.parse_args();print(make(a.out.resolve()))
