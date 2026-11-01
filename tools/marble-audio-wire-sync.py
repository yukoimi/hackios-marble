"""Copy the tested pure AudioReach wire codecs into the XNU source overlay.

The firmware headers are authoritative. No firmware service, MMIO or ABI
adapter is imported into XNU by this operation. --verify detects drift.
"""
import argparse,hashlib,json,re
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--verify',action='store_true');a=p.parse_args()
 source=ROOT/'xnuloader';target=ROOT/'xnu/files/bsd/sys/marble_audio_wire'
 pending=['marble_tdm_run.h','marble_pcm_memory.h','marble_pcm_data.h','marble_glink_data.h'];files={}
 while pending:
  name=pending.pop()
  if name in files:continue
  if Path(name).name!=name or not name.endswith('.h') or '_fw.' in name:raise ValueError('non-wire include: '+name)
  raw=(source/name).read_bytes();text=raw.decode()
  if any(word in text for word in ('__asm__','READ_SYSREG','clean_dcache','EFIAPI')):raise ValueError('hardware adapter in wire closure: '+name)
  files[name]=raw;pending.extend(re.findall(r'#include "([^"]+)"',text))
 manifest={name:dict(sha256=hashlib.sha256(raw).hexdigest(),bytes=len(raw)) for name,raw in sorted(files.items())}
 if a.verify:
  assert json.loads((target/'sources.json').read_text())==manifest,'wire manifest drift'
  for name,raw in files.items():assert (target/name).read_bytes()==raw,name
 else:
  target.mkdir(exist_ok=True)
  for name,raw in files.items():(target/name).write_bytes(raw)
  (target/'sources.json').write_text(json.dumps(manifest,indent=2)+'\n')
 print(('Verified' if a.verify else 'Synced'),len(files),'pure wire headers; no hardware operation')
if __name__=='__main__':main()
