"""Reassemble overlapping raw live logs without inventing or rewriting evidence."""
import argparse,hashlib,json
from pathlib import Path
HEAD=256*1024;CAP=HEAD+1024*1024
LIVE=b'== KERNEL LOG (the kernel is still running) ==\n'
FINAL=b'== KERNEL LOG ==\n'
NOTE=b'\n\n[... the log between the first 256 KB and the last 1 MB was dropped ...]\n\n'
END=b'\n== END OF LOG ==\n'
def sha(data):return hashlib.sha256(data).hexdigest()
def join(paths,output):
    assembled=b'';identity=None;records=[];final_header=None;final_end=b''
    for number,path in enumerate(paths):
        raw=path.read_bytes();marker=LIVE if LIVE in raw else FINAL
        if raw.count(marker)!=1:raise ValueError('one kernel marker required: '+str(path))
        start=raw.index(marker)+len(marker);body=raw[start:]
        if END in body:body=body[:body.index(END)]
        if identity is None:
            if len(body)>=CAP or NOTE in body or len(body)<8192:raise ValueError('first log must be an unwrapped prefix')
            identity=body[:4096]
        if not body.startswith(identity):raise ValueError('kernel boot prefix differs: '+str(path))
        if marker==FINAL:
            if number!=len(paths)-1:raise ValueError('returned log must be last')
            final_header=raw[:start];final_end=raw[raw.index(END,start):] if END in raw[start:] else b''
        offset=0
        if NOTE in body:
            if body.index(NOTE)!=HEAD:raise ValueError('unexpected dropped-log boundary')
            offset=HEAD+len(NOTE)
        elif len(body)>=CAP:offset=HEAD
        if offset:offset=body.index(b'\n',offset)+1 # oldest ring line may be partial
        stop=body.rfind(b'\n')+1
        candidate=body[offset:stop]
        append_at=0;overlap=0
        if assembled:
            for length in (65536,16384,4096,1024):
                length=min(length,len(assembled));anchor=assembled[-length:]
                if candidate.count(anchor)!=1:continue
                at=candidate.index(anchor)
                # The oldest ring bytes can be overwritten while the loader
                # copies a live snapshot. They are never appended. Require an
                # exact unique suffix anchor, then copy only the newer bytes.
                # Missing newest evidence still has no anchor and is rejected.
                overlap=length;append_at=at+length;break
            else:raise ValueError('no exact continuous overlap; evidence gap at '+str(path))
        appended=candidate[append_at:]
        records.append(dict(path=str(path.resolve()),sha256=sha(raw),
            copy_offset=start+offset+append_at,copy_bytes=len(appended),overlap_bytes=overlap))
        assembled+=appended
    if final_header is None or not final_end:raise ValueError('complete returned log required')
    result=final_header+assembled+final_end
    output.write_bytes(result)
    output.with_suffix('.provenance.json').write_text(json.dumps(dict(
        operation='exact-continuous-overlap-of-complete-raw-lines',
        header_source=str(paths[-1].resolve()),records=records,
        output=dict(path=str(output.resolve()),bytes=len(result),sha256=sha(result))),indent=2)+'\n')
    print('Assembled',len(result),'bytes from',len(paths),'raw logs with continuous exact overlap')
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--live-dir',required=True,type=Path)
    p.add_argument('--returned',required=True,type=Path);p.add_argument('--output',required=True,type=Path);a=p.parse_args()
    join(sorted(a.live_dir.glob('*.log'))+[a.returned],a.output)
if __name__=='__main__':main()
