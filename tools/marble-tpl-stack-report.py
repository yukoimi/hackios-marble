"""Decode bounded RAM stack snapshots offline; never contact the phone.

The exception layout is inferred from the original Mu ArmCpuDxe vector:
GPRs 0..27, x28/x29, x30/SP, 32 Q registers, ELR/SPSR/FPSR/ESR/FAR.
Raw executable-section matches can also be data. Only validated frame-record
links are labelled return addresses, and call instructions are checked.
"""
import argparse,json,re,struct
from pathlib import Path

def pe_word(path,rva):
 data=path.read_bytes();pe=struct.unpack_from('<I',data,60)[0]
 count=struct.unpack_from('<H',data,pe+6)[0];size=struct.unpack_from('<H',data,pe+20)[0]
 for i in range(count):
  at=pe+24+size+40*i;_,va,length,raw=struct.unpack_from('<4I',data,at+8)
  if va<=rva and rva+4<=va+length:return struct.unpack_from('<I',data,raw+rva-va)[0]
 return None

def inspect(log,mapping):
 catalog=json.loads((mapping/'index.json').read_text());owners={}
 for m in catalog['matches']:
  if m['unique_owner']:owners[int(m['observation']['callback'],16)]=m['owners'][0]
 def identify(address,return_address=False):
  result=dict(address=hex(address));owner=owners.get(address)
  if not owner:return result
  result.update(image=owner['name'],rva=owner['rva'])
  if return_address:
   rva=int(owner['rva'],16)
   word=pe_word(mapping/owner['file'],rva-4) if rva>=4 else None
   result['preceded_by_call']=word is not None and (word&0xfc000000==0x94000000 or word&0xfffffc1f==0xd63f0000)
  return result
 stacks={}
 for line in log.read_text(errors='replace').splitlines():
  row=dict(re.findall(r'(\w+)=([^\s]+)',line))
  if line.startswith('aud-tpl-stack: '):
   index=int(row['sample'])
   if index in stacks:raise ValueError('duplicate stack snapshot')
   stacks[index]=dict(sp=int(row['sp'],16),count=int(row['words']),words={})
  elif line.startswith('aud-tpl-stack-word: '):
   s=stacks[int(row['sample'])];offset=int(row['offset'])
   if offset in s['words'] or offset&7 or offset>=2048:raise ValueError('invalid stack word offset')
   s['words'][offset]=int(row['value'],16)
 results=[]
 for index,s in stacks.items():
  words=s['words'];sp=s['sp'];contexts=[]
  if sorted(words)!=list(range(0,s['count']*8,8)) or s['count']>256:raise ValueError('incomplete stack words')
  for at in range(0,s['count']*8-0x328,8):
   original_sp=words.get(at+0xf8);pc=words.get(at+0x300,0);pstate=words.get(at+0x308,0)
   if original_sp!=sp+at+0x330 or original_sp&15 or pc&3 or pc not in owners or pstate&15 not in (5,9):continue
   fp=words.get(at+0xe8,0);frames=[];seen=set()
   for _ in range(16):
    offset=fp-sp
    if fp in seen or fp&7 or offset<at+0x330 or offset not in words or offset+8 not in words:break
    seen.add(fp);next_fp=words[offset];lr=words[offset+8]
    frames.append(dict(fp=hex(fp),return_address=identify(lr,True)))
    if next_fp<=fp:break
    fp=next_fp
   contexts.append(dict(offset=at,original_sp=hex(original_sp),pc=identify(pc),pstate=hex(pstate),frames=frames,
                        frame_chain_end='outside bounded snapshot, invalid link, or depth limit'))
  results.append(dict(sample=index,sp=hex(sp),words=s['count'],candidate_contexts=contexts))
 return dict(scope='offline inferred original Mu exception layout; bounded frame links, no hardware execution or callback changes',samples=results)

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--log',type=Path,required=True);p.add_argument('--map',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
 result=inspect(a.log,a.map);a.out.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
if __name__=='__main__':main()
