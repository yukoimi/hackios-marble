"""Losslessly deduplicate constant log prefixes in a separate EFI source tree."""
import hashlib,json,re,shutil
from pathlib import Path

PATTERN=re.compile(r'(?<=\bprint\(")(marble[-\w]*: )')
FORMAT=re.compile(r'(?<=\bprint\(")(?:[^"\\]|\\.)*')
TOKEN=re.compile(r'\\001\\([0-7]{3})')

def transform(text,prefixes):
 index={p:i for i,p in enumerate(prefixes)}
 pieces=re.compile('|'.join(re.escape(p) for p in sorted(prefixes,key=len,reverse=True)))
 def encode_format(m):return pieces.sub(lambda x:'\\001\\%03o'%index[x[0]],m[0])
 packed=FORMAT.sub(encode_format,text)
 # Independently decode each transformed occurrence, and compare every byte.
 restored=FORMAT.sub(lambda f:TOKEN.sub(lambda m:prefixes[int(m[1],8)],f[0]),packed)
 if restored!=text:raise ValueError('lossless log-prefix reconstruction failed')
 return packed

def create(source,destination,reference=None):
 source=Path(source);destination=Path(destination)
 if destination.exists():raise ValueError('new packed input directory required')
 files=sorted(p for p in source.rglob('*') if p.is_file())
 texts={p:p.read_text(encoding='utf8') for p in files if p.suffix in ('.c','.h')}
 prefixes=sorted({m[0] for s in texts.values() for m in PATTERN.finditer(s)})
 reference_bytes=Path(reference).read_bytes() if reference is not None else None
 if reference_bytes is not None:
  # Account for the one dictionary string and pointer, plus the two-byte
  # marker in each format. Do not retain dead profile-specific prefixes.
  fixed={m[0].decode('ascii') for m in re.finditer(rb' [A-Za-z_]+=[0-9a-f]+(?=[ \r\n\x00])',reference_bytes)}
  formats=[m[0] for s in texts.values() for m in FORMAT.finditer(s)]
  prefixes=sorted(p for p in set(prefixes)|fixed if any(p in f for f in formats) and (reference_bytes.count(p.encode())-1)*len(p)-8-2*reference_bytes.count(p.encode())>=16)
 if not 0<len(prefixes)<=255:raise ValueError('prefix dictionary bounds')
 shutil.copytree(source,destination)
 records={};saved=0
 for p,s in texts.items():
  packed=transform(s,prefixes);target=destination/p.relative_to(source)
  # Keep the original encoding/line endings outside transformed ASCII spans.
  original=p.read_bytes();raw=transform(original.decode('utf8'),prefixes).encode('utf8')
  if raw.decode('utf8').replace('\r\n','\n')!=packed:raise ValueError('source text encoding mismatch')
  target.write_bytes(raw);saved+=sum(len(prefixes[int(m[1],8)])-2 for m in TOKEN.finditer(packed))
  records[p.relative_to(source).as_posix()]=dict(original_sha256=hashlib.sha256(original).hexdigest(),packed_sha256=hashlib.sha256(raw).hexdigest())
 header='#define MARBLE_LOG_PREFIX_COUNT %d\nstatic const char *const marble_log_prefixes[]={\n'%len(prefixes)
 header+=','.join(json.dumps(s,ensure_ascii=True) for s in prefixes)+'\n};\n'
 (destination/'marble_log_prefixes.h').write_text(header,encoding='ascii')
 result=dict(prefixes=prefixes,source_files=records,raw_literal_bytes_saved=saved,reference_sha256=hashlib.sha256(reference_bytes).hexdigest() if reference_bytes is not None else None,scope='print-format prefix bytes only; every original source reconstructed exactly')
 (destination.parent/'log-prefix-pack.json').write_text(json.dumps(result,indent=2)+'\n')
 return result
