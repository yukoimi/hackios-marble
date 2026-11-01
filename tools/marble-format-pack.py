"""Pack complete constant print formats using the pinned LZ4 block codec.

Only a complete first literal argument is replaced. Every source is restored
byte for byte with its per-occurrence literal records before it is accepted.
"""
import hashlib,json,re,shutil,subprocess
from pathlib import Path
FORMAT=re.compile(r'\bprint\("((?:[^"\\]|\\.)*)"(\s*)(?=[,)])')

def literal_bytes(raw):
 out=bytearray();i=0;esc={'a':7,'b':8,'f':12,'n':10,'r':13,'t':9,'v':11,'\\':92,'"':34,"'":39,'?':63}
 while i<len(raw):
  if raw[i]!='\\':out.extend(raw[i].encode('utf8'));i+=1;continue
  i+=1
  if i>=len(raw):return None
  c=raw[i]
  if c in esc:out.append(esc[c]);i+=1
  elif c in '01234567':
   end=i+1
   while end<min(i+3,len(raw)) and raw[end] in '01234567':end+=1
   n=int(raw[i:end],8)
   if n>255:return None
   out.append(n);i=end
  elif c=='x':
   end=i+1
   while end<len(raw) and raw[end] in '0123456789abcdefABCDEF':end+=1
   if end==i+1:return None
   n=int(raw[i+1:end],16)
   if n>255:return None
   out.append(n);i=end
  else:return None
 return bytes(out)

def transform(text,formats):
 index={s:i for i,s in enumerate(formats)};records=[]
 def replace(m):
  value=literal_bytes(m[1])
  if value not in index:return m[0]
  n=index[value];encoded='\\002\\%03o\\%03o'%(n&255,n>>8)
  records.append(dict(index=n,literal=m[1],offset=m.start()))
  return 'print("'+encoded+'"'+m[2]
 packed=FORMAT.sub(replace,text);cursor=0
 def restore(m):
  nonlocal cursor
  value=literal_bytes(m[1])
  if value is None or len(value)!=3 or value[0]!=2:return m[0]
  if cursor>=len(records):raise ValueError('unowned compact format')
  row=records[cursor];cursor+=1;n=value[1]|value[2]<<8
  if n!=row['index'] or literal_bytes(row['literal'])!=formats[n]:raise ValueError('format reconstruction identity')
  return 'print("'+row['literal']+'"'+m[2]
 restored=FORMAT.sub(restore,packed)
 if restored!=text or cursor!=len(records):raise ValueError('source reconstruction failed')
 return packed,records

def create(source,destination,reference,codec):
 source=Path(source);destination=Path(destination);reference=Path(reference);codec=Path(codec).resolve()
 if destination.exists():raise ValueError('new packed tree required')
 texts={p:p.read_bytes().decode('utf8') for p in source.rglob('*') if p.is_file() and p.suffix in ('.c','.h')}
 binary=reference.read_bytes();candidates={literal_bytes(m[1]) for s in texts.values() for m in FORMAT.finditer(s)}
 formats=sorted(s for s in candidates if s is not None and len(s)>=20 and b'\0' not in s and s+b'\0' in binary)
 if not 0<len(formats)<=65535:raise ValueError('format table count bounds')
 offsets=[];raw=bytearray()
 for s in formats:offsets.append(len(raw));raw.extend(s+b'\0')
 if len(raw)>1048576:raise ValueError('format table size bounds')
 destination.parent.mkdir(exist_ok=True);raw_path=destination.parent/'formats.raw';compressed=destination.parent/'formats.lz4';roundtrip=destination.parent/'formats-roundtrip.raw'
 raw_path.write_bytes(raw)
 subprocess.run([str(codec),'c',str(raw_path),str(compressed),'1048576'],check=True)
 subprocess.run([str(codec),'d',str(compressed),str(roundtrip),str(len(raw))],check=True)
 if roundtrip.read_bytes()!=raw:raise ValueError('LZ4 roundtrip mismatch')
 shutil.copytree(source,destination);records={}
 for p,s in texts.items():
  packed,literals=transform(s,formats);data=packed.encode('utf8');target=destination/p.relative_to(source);target.write_bytes(data)
  records[p.relative_to(source).as_posix()]=dict(original_sha256=hashlib.sha256(s.encode('utf8')).hexdigest(),packed_sha256=hashlib.sha256(data).hexdigest(),replacements=literals)
 block=compressed.read_bytes()
 header='#define MARBLE_FORMAT_COUNT %d\n#define MARBLE_FORMAT_RAW_SIZE %d\n'%(len(formats),len(raw))
 offset_type='uint16_t' if len(raw)<=65536 else 'uint32_t'
 header+='static const '+offset_type+' marble_format_offsets[]={'+','.join(str(n) for n in offsets)+'};\n'
 header+='static const uint8_t marble_format_block[]={\n'
 header+='\n'.join(','.join('0x%02x'%n for n in block[i:i+24])+',' for i in range(0,len(block),24))+'\n};\n'
 (destination/'marble_log_formats.h').write_text(header,encoding='ascii')
 result=dict(formats=len(formats),raw_bytes=len(raw),compressed_bytes=len(block),raw_sha256=hashlib.sha256(raw).hexdigest(),compressed_sha256=hashlib.sha256(block).hexdigest(),reference_sha256=hashlib.sha256(binary).hexdigest(),source_files=records,scope='lossless complete constant print formats; arguments and protocols unchanged')
 (destination.parent/'format-pack.json').write_text(json.dumps(result,indent=2)+'\n');return result
