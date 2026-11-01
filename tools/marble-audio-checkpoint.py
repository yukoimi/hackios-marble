"""Archive current sources and explicit local audio evidence; verify every member.

Files are stored as bytes, with Git modes recorded separately (including
symlink modes). This is a source/evidence checkpoint with a parent, not a
claim that an untested kernel or audio provider is ready for use.
"""
import argparse,hashlib,importlib.util,io,json,tarfile
from datetime import datetime,timezone
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def sha(raw):return hashlib.sha256(raw).hexdigest()
def verify(target):
 manifest=json.loads((target/'manifest.json').read_text())
 for name,item in manifest['files'].items():
  assert Path(name).name==name
  raw=(target/name).read_bytes();assert len(raw)==item['bytes'] and sha(raw)==item['sha256'],name
 for label in ('sources','evidence'):
  items=json.loads((target/(label+'-files.json')).read_text())
  with tarfile.open(target/(label+'.tar.gz'),'r:gz') as tf:
   assert set(tf.getnames())==set(items) and len(tf.getnames())==len(items)
   for member in tf:
    assert member.isfile();raw=tf.extractfile(member).read();item=items[member.name]
    assert len(raw)==item['bytes'] and sha(raw)==item['sha256'],member.name
 return manifest
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('directory',type=Path)
 p.add_argument('--verify',action='store_true');p.add_argument('--parent',type=Path)
 p.add_argument('--evidence',type=Path,nargs='+');p.add_argument('--status');a=p.parse_args()
 target=a.directory.resolve()
 if a.verify:
  m=verify(target);print('Verified',m['source_members'],'sources and',m['evidence_members'],'evidence members');return
 if not a.parent or not a.evidence or not a.status:p.error('--parent, --evidence and --status are required')
 parent=a.parent.resolve()/'manifest.json';parent_sha=sha(parent.read_bytes())
 entries={}
 for requested in a.evidence:
  path=requested.resolve()
  if not path.is_relative_to(ROOT/'out') or not path.exists():p.error('evidence must exist under workspace out/: '+str(path))
  for file in ([path] if path.is_file() else path.rglob('*')):
   if file.is_file():entries[file.relative_to(ROOT).as_posix()]=(file,'100644')
 target.mkdir(exist_ok=False)
 spec=importlib.util.spec_from_file_location('audio_checkpoint_sources',ROOT/'tools/marble-checkpoint.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
 def pack(label,files):
  items={}
  with tarfile.open(target/(label+'.tar.gz'),'w:gz') as tf:
   for name,(path,mode) in sorted(files.items()):
    raw=path.read_bytes();member=tarfile.TarInfo(name);member.size=len(raw);member.mode=0o755 if mode=='100755' else 0o644
    tf.addfile(member,io.BytesIO(raw));items[name]=dict(bytes=len(raw),sha256=sha(raw),git_mode=mode)
  (target/(label+'-files.json')).write_text(json.dumps(items,indent=2)+'\n');return len(items)
 sources=pack('sources',m.sources());evidence=pack('evidence',entries)
 manifest=dict(created_utc=datetime.now(timezone.utc).isoformat(),status=a.status,
  prerequisite=dict(path=str(parent.parent),manifest_sha256=parent_sha),source_members=sources,evidence_members=evidence,
  audio_acceptance=False,continuous_playback=False,files={})
 for file in target.iterdir():
  if file.is_file():manifest['files'][file.name]=dict(bytes=file.stat().st_size,sha256=sha(file.read_bytes()))
 (target/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n');verify(target)
 print('Saved and verified',sources,'sources and',evidence,'evidence members:',target)
if __name__=='__main__':main()
