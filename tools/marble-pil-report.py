"""Decode exact native 0x118-byte PIL configuration copies, without device I/O."""
import argparse,json,re,struct,uuid
from pathlib import Path
def inspect(path):
    text=path.read_text(encoding='utf8',errors='replace');result={}
    for name in ('FULL_ADSP2','FULL_SLPI'):
        rows=re.findall(r'marble-pil-config: name='+name+r' offset=([0-9a-f]+) word=([0-9a-f]+)',text)
        words={int(offset,16):int(value,16) for offset,value in rows}
        if set(words)!=set(range(0,0x118,8)):raise ValueError('incomplete exact config copy: '+name)
        data=b''.join(struct.pack('<Q',words[offset]) for offset in range(0,0x118,8))
        result[name]=dict(name=data[0xc:0x4c].decode('utf-16le').split('\0')[0],
            image_type=struct.unpack_from('<I',data,8)[0],
            partition_guid=str(uuid.UUID(bytes_le=data[0x9c:0xac])),
            image_path=data[0xac:0xec].decode('utf-16le').split('\0')[0],
            pas_id=struct.unpack_from('<I',data,0xec)[0],
            carveout_pa=hex(struct.unpack_from('<Q',data,0xf0)[0]),
            carveout_bytes=struct.unpack_from('<Q',data,0xf8)[0],
            record_image_load_info=struct.unpack_from('<H',data,0x100)[0],
            override_elf_address=struct.unpack_from('<H',data,0x102)[0],
            proxy_guid=str(uuid.UUID(bytes_le=data[0x104:0x114])))
    return dict(configurations=result,exact_native_table=True,
        scope='native RAM configuration copies only; no DSP image loaded',
        important_difference='native FULL_SLPI uses slpi, Android evidence selects slpi2; do not start this config unchanged',
        dsp_started=False,peripheral_functional_acceptance=False)
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('log',type=Path);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
    if a.out.exists():p.error('new report required')
    result=inspect(a.log);a.out.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
if __name__=='__main__':main()
