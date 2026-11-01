"""Record the exact local adapter delta against the pinned NativeAdreno ring."""
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
COMMIT = 'b686b9054f57d393c80195b82568a09f8ca106a2'


def main():
    upstream = ROOT.parent/'upstream-marble/projects/NativeAdreno'
    target = ROOT/'xnu/files/bsd/dev/marble_gpu/upstream'
    pins = json.loads((upstream.parent/'sources.json').read_text(encoding='utf8'))
    pin = next(p for p in pins if p['repository']=='https://github.com/UniXNU/NativeAdreno')
    if pin['commit'] != COMMIT:
        raise ValueError('upstream archive commit differs from the reviewed pin')
    hashes = {f['path']: f['sha256'] for f in pin['files']}
    record = dict(repository='https://github.com/UniXNU/NativeAdreno', commit=COMMIT,
                  changes='adoptIdle declaration/implementation and read-only writePointer accessor; original packet framing retained',
                  archive_sha256=pin['archive_sha256'], files=[])
    for name in ('NativeAdrenoRing.cpp', 'NativeAdrenoRing.h'):
        # The local upstream came from an archive, not a Git checkout. Check
        # its bytes against the existing archive inventory before diffing.
        original = (upstream/name).read_bytes()
        if hashlib.sha256(original).hexdigest() != hashes[name]:
            raise ValueError('upstream bytes differ from pinned inventory: '+name)
        adapted = (target/name).read_bytes()
        record['files'].append(dict(path=name, upstream_sha256=hashlib.sha256(original).hexdigest(),
                                    adapted_sha256=hashlib.sha256(adapted).hexdigest()))
        import difflib
        patch = ''.join(difflib.unified_diff(original.decode().splitlines(keepends=True),
                                           adapted.decode().splitlines(keepends=True),
                                           fromfile='a/'+name, tofile='b/'+name))
        (target/(name+'.marble.patch')).write_text(patch, encoding='utf8', newline='\n')
    (target/'native-ring-source.json').write_text(json.dumps(record, indent=2)+'\n', encoding='utf8')
    print(json.dumps(record, indent=2))


if __name__ == '__main__':
    main()
