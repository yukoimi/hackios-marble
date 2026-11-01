"""Pack verified ADSP2/SLPI2 ROM inputs for an isolated read-only EFI RAM filesystem.

No phone I/O and no DSP start. Inputs remain local.
"""
import argparse,hashlib,json,re,struct
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def fnv(data):
    v=0xcbf29ce484222325
    for b in data:v=((v^b)*0x100000001b3)&0xffffffffffffffff
    return v
def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--material',type=Path,default=Path('out/n12turbo-rom/daily-material/firmware'))
    p.add_argument('--out',type=Path,required=True);a=p.parse_args();out=a.out.resolve()
    if not out.is_relative_to(ROOT/'out') or out.exists():p.error('new directory under out required')
    catalog=json.loads((a.material/'manifest.json').read_text());rows=[]
    for row in catalog:
        if re.fullmatch(r'image/(adsp2|slpi2)\.(mdt|b\d\d)',row['file']):rows.append(row)
    rows.sort(key=lambda r:r['file'])
    if len(rows)!=51:raise ValueError('expected exactly the qualified 27 ADSP2 and 24 SLPI2 files')
    data=bytearray((32+len(rows)*64+4095)&~4095);entries=[]
    for i,row in enumerate(rows):
        raw=(a.material/row['file']).read_bytes()
        if len(raw)!=row['size'] or hashlib.sha256(raw).hexdigest()!=row['sha256']:raise ValueError('material differs: '+row['file'])
        offset=len(data);name=row['file'].split('/')[-1]
        struct.pack_into('<32sQQQQ',data,32+i*64,name.encode(),offset,len(raw),fnv(raw),0)
        entries.append(dict(name=name,offset=offset,bytes=len(raw),sha256=row['sha256']))
        data.extend(raw);data.extend(bytes((-len(data))%8))
    if len(data)>0x3f00000:raise ValueError('payload staging size exceeded')
    struct.pack_into('<8sIIQQ',data,0,b'MPILFS1\0',len(rows),64,len(data),0)
    out.mkdir();(out/'firmware-files.bin').write_bytes(data)
    result=dict(source_manifest=str(a.material/'manifest.json'),source_manifest_sha256=hashlib.sha256((a.material/'manifest.json').read_bytes()).hexdigest(),
        bytes=len(data),sha256=hashlib.sha256(data).hexdigest(),fnv1a64=hex(fnv(data)),files=entries,
        scope='host-packed verified ROM inputs, read-only RAM filesystem candidate; no DSP execution')
    (out/'manifest.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps({k:v for k,v in result.items() if k!='files'},indent=2))
if __name__=='__main__':main()
