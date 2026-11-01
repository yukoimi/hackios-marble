"""Map passive callback code prefixes to the pinned original Mu PE images.

Offline only. A matching owner identifies a candidate callback, not its
duration or permission to call, cancel, patch, or execute that callback.
"""
import argparse,hashlib,importlib.util,json,lzma,re,struct,uuid,zlib
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def executable_rva(data,offset,size):
 if data[:2]!=b'MZ' or len(data)<512:return None
 pe=struct.unpack_from('<I',data,60)[0]
 if pe+24>len(data) or data[pe:pe+4]!=b'PE\0\0':return None
 count=struct.unpack_from('<H',data,pe+6)[0];header=struct.unpack_from('<H',data,pe+20)[0]
 if count>128 or pe+24+header+40*count>len(data):return None
 for i in range(count):
  at=pe+24+header+40*i;_,rva,length,raw=struct.unpack_from('<4I',data,at+8);flags=struct.unpack_from('<I',data,at+36)[0]
  if flags&0x20000000 and raw<=offset and offset+size<=raw+length:return rva+offset-raw
 return None
def build(source,out,log=None,loader=None):
 out=out.resolve();out.mkdir(exist_ok=False)
 spec=importlib.util.spec_from_file_location('tpl_image_extract',ROOT/'tools/marble-image.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
 image=source.read_bytes()
 if hashlib.sha256(image).hexdigest()!=m.SOURCE_SHA256:raise ValueError('original Mu image identity')
 ks=struct.unpack_from('<I',image,8)[0];raw=zlib.decompress(image[4096:4096+ks],31);fv=raw[0x70:]
 p,n=next((p,n) for p,n,g in m.files_in_fv(fv,0) if fv[p+18]==11);sec=p+24;size=int.from_bytes(fv[sec:sec+3],'little');offset=struct.unpack_from('<H',fv,sec+20)[0]
 inner=lzma.decompress(fv[sec+offset:sec+size]);images=[]
 for label,data,base in [('outer',fv,0),('inner',inner,8)]:
  for p,n,guid in m.files_in_fv(data,base):
   at=p+24;payloads=[];name=''
   while at+4<=p+n:
    length=int.from_bytes(data[at:at+3],'little');kind=data[at+3]
    if length<4 or at+length>p+n:break
    payload=data[at+4:at+length]
    if kind==16 and payload[:2]==b'MZ':payloads.append(payload)
    if kind==21:name=payload.decode('utf-16le',errors='replace').rstrip('\0')
    at=(at+length+3)&~3
   for i,payload in enumerate(payloads):
    file=out/(label+'-'+str(uuid.UUID(bytes_le=guid))+'-'+str(i)+'.efi');file.write_bytes(payload)
    images.append(dict(file=file.name,name=name or str(uuid.UUID(bytes_le=guid)),sha256=hashlib.sha256(payload).hexdigest(),bytes=len(payload)))
 if loader:
  payload=loader.read_bytes();file=out/'candidate-loader.efi';file.write_bytes(payload);images.append(dict(file=file.name,name='candidate loader',sha256=hashlib.sha256(payload).hexdigest(),bytes=len(payload)))
 matches=[]
 if log:
  text=log.read_bytes().split(b'\0PMOC')[0].decode(errors='replace')
  for line in text.splitlines():
   if not any(prefix in line for prefix in ('aud-tpl-event: ','aud-tpl-restore-code: ','aud-tpl-stack-code: ')):continue
   row=dict(re.findall(r'(\w+)=([^\s]+)',line));words=row['code'].split(',')
   row.setdefault('phase','qualify');row.setdefault('index','0')
   if len(words)!=8:raise ValueError('callback prefix length')
   prefix=b''.join(int(word,16).to_bytes(8,'little') for word in words);callback=int(row['callback'],16);owners=[]
   for image in images:
    data=(out/image['file']).read_bytes();position=0
    while True:
     position=data.find(prefix,position)
     if position<0:break
     rva=executable_rva(data,position,len(prefix))
     if rva is not None and callback>=rva and not ((callback-rva)&4095):owners.append(dict(name=image['name'],file=image['file'],rva=hex(rva),runtime_base=hex(callback-rva),sha256=image['sha256']))
     position+=4
   matches.append(dict(observation=row,owners=owners,unique_owner=len(owners)==1))
 result=dict(scope='offline owner candidates from 64-byte passive code prefixes; no callback execution, timer changes, phone or partition operations',images=images,matches=matches)
 (out/'index.json').write_text(json.dumps(result,indent=2)+'\n');return result
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--source',type=Path,required=True);p.add_argument('--out',type=Path,required=True);p.add_argument('--log',type=Path);p.add_argument('--loader',type=Path);a=p.parse_args();r=build(a.source,a.out,a.log,a.loader)
 print(json.dumps(dict(images=len(r['images']),matches=[dict(phase=x['observation']['phase'],index=x['observation']['index'],owners=x['owners']) for x in r['matches']]),indent=2))
if __name__=='__main__':main()
