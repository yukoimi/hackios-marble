"""Generate/check digest pins from the exact supplied native RPMh executable."""
import argparse,hashlib,json,struct
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
RANGES=[(0x17b4,0x18),(0x1890,0x3c),(0x2b80,0x104),(0x2eb0,0x320),
        (0x327c,0x94),(0x3f08,0x470),(0x4278,0x30),(0x4338,0xa0),
        (0x4758,0x220),(0x4c60,0x32c),(0x4fec,0x184),(0x5a10,0x54)]
def render():
 data=(ROOT/'out/peripheral-power-audit-v1-plan/RpmhDxe.efi').read_bytes()
 if hashlib.sha256(data).hexdigest()!='7f917c9e9c64ba11bee99f3d56f5474d39dac210f5a2dfad2059dd1d4afcd3c6':raise ValueError('native image hash')
 pe=struct.unpack_from('<I',data,60)[0];section=pe+24+struct.unpack_from('<H',data,pe+20)[0]
 sections=[struct.unpack_from('<IIII',data,section+40*i+8) for i in range(struct.unpack_from('<H',data,pe+6)[0])]
 rows=[];lines=['/* Exact Mu RpmhDxe wrapper/submit/cache/completion code digest pins. */',
  'static const struct {unsigned rva,bytes;uint8_t digest[32];} marble_xo_rpmh_code[]={']
 for rva,size in RANGES:
  matches=[data[off+rva-va:off+rva-va+size] for vs,va,n,off in sections if va<=rva and rva+size<=va+min(vs,n)]
  if len(matches)!=1 or len(matches[0])!=size:raise ValueError('code extent')
  digest=hashlib.sha256(matches[0]).digest()
  rows.append(dict(rva=rva,bytes=size,sha256=digest.hex()))
  lines.append('    {0x%x,%d,{%s}},'%(rva,size,','.join('0x%02x'%b for b in digest)))
 lines.append('};');return '\n'.join(lines)+'\n',json.dumps(rows,indent=2)+'\n'
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--write',action='store_true');a=p.parse_args()
 code,metadata=render()
 for dest,content in [(ROOT/'xnuloader/marble_xo_rpmh_code.h',code),(ROOT/'out/adsp-native-dependencies-v2-plan/xo-rpmh-code-ranges.json',metadata)]:
  if a.write:dest.write_text(content,encoding='utf8')
  elif dest.read_text(encoding='utf8')!=content:raise ValueError('digest metadata differs: '+str(dest))
 print('PASS',len(RANGES),'RPMh physical XO code ranges')
if __name__=='__main__':main()
