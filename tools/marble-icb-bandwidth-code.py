"""Pin exact native NPA vector/complete and ICB request conversion code."""
import argparse,hashlib,json,struct
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
INPUTS=[('npa','Npa','9ced898ce160f69aede9ce1ad93dfef9223e9c280dc3ec99616861eae56e9c09',[(0x162c,24),(0x37ac,0x22c),(0x1474,24),(0x3a5c,0x80),(0xa138,0x78)]),
 ('icb','ICB','6cd27554f8edcf3bbe41473448354e55cd57461f0c29abbe18a5a8b6fdca596a',[(0x2b50,0x2b0),(0x310c,0x12c),(0x415c,0xe8),(0x3734,0xc4),(0x5250,0x11c),(0x5538,0x3cc),(0x5ba4,0x200)])]
def render():
 lines=['/* Exact Mu vector/complete and ICB type3 conversion code. */'];meta={}
 for name,file,sha,ranges in INPUTS:
  data=(ROOT/f'out/peripheral-power-audit-v1-plan/{file}Dxe.efi').read_bytes()
  if hashlib.sha256(data).hexdigest()!=sha:raise ValueError('native image hash: '+file)
  pe=struct.unpack_from('<I',data,60)[0];section=pe+24+struct.unpack_from('<H',data,pe+20)[0]
  sections=[struct.unpack_from('<IIII',data,section+40*i+8) for i in range(struct.unpack_from('<H',data,pe+6)[0])]
  lines.append('static const struct {unsigned rva,bytes;uint8_t digest[32];} marble_icb_bandwidth_'+name+'_code[]={');meta[name]=[]
  for rva,size in ranges:
   matches=[data[off+rva-va:off+rva-va+size] for vs,va,n,off in sections if va<=rva and rva+size<=va+min(vs,n)]
   if len(matches)!=1 or len(matches[0])!=size:raise ValueError('code extent')
   digest=hashlib.sha256(matches[0]).digest();meta[name].append(dict(rva=rva,bytes=size,sha256=digest.hex()))
   lines.append('    {0x%x,%d,{%s}},'%(rva,size,','.join('0x%02x'%b for b in digest)))
  lines.append('};')
 return '\n'.join(lines)+'\n',json.dumps(meta,indent=2)+'\n'
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--write',action='store_true');a=p.parse_args();code,meta=render()
 for path,content in [(ROOT/'xnuloader/marble_icb_bandwidth_code.h',code),(ROOT/'out/adsp-native-dependencies-v2-plan/icb-bandwidth-code-ranges.json',meta)]:
  if a.write:path.write_text(content,encoding='utf8')
  elif path.read_text(encoding='utf8')!=content:raise ValueError('metadata mismatch: '+str(path))
 print('PASS native vector/complete and ICB bandwidth code pins')
if __name__=='__main__':main()
