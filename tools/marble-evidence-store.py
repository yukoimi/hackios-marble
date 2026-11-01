"""Verify or extract historical evidence after lossless archive consolidation.

Original checkpoint manifests stay unchanged. This verifies member content,
not byte-identical reconstruction of a removed gzip container. Extraction
always creates a new directory and never replaces live hardware status.
"""
import argparse
import base64
import hashlib
import json
from pathlib import Path, PurePosixPath
import tarfile

ROOT = Path(__file__).resolve().parents[1]
DEFAULT = ROOT / 'out/compacted-evidence-20261101/catalog.json'


def sha(path):
    h = hashlib.sha256()
    with Path(path).open('rb') as f:
        for block in iter(lambda: f.read(1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()


def member_name(name):
    name = name.replace('\\', '/')
    parts = name.split('/')
    if not name or any(p in ('', '.', '..') or ':' in p for p in parts):
        raise ValueError('unsafe member: ' + name)
    return PurePosixPath(name).as_posix()


def inside(root, name):
    p = (root / member_name(name)).resolve()
    if not p.is_relative_to(root.resolve()):
        raise ValueError('path escapes evidence root')
    return p


def index(path):
    result = {}
    for name, item in json.loads(path.read_text()).items():
        key = member_name(name)
        if key in result:
            raise ValueError('duplicate normalized member')
        result[key] = item
    return result


def same(a, b):
    return a['bytes'] == b['bytes'] and a['sha256'] == b['sha256']


def check_file(path, expected):
    if path.stat().st_size != expected['bytes'] or sha(path) != expected['sha256']:
        raise ValueError('identity mismatch: ' + str(path))


def load(catalog, root=ROOT):
    data = json.loads(Path(catalog).read_text())
    if data['schema'] != 'marble-evidence-storage-v1':
        raise ValueError('unsupported catalog')
    anchors = {}
    for name, item in data['anchors'].items():
        checkpoint = inside(root, item['checkpoint'])
        check_file(checkpoint / 'manifest.json', item['manifest'])
        manifest = json.loads((checkpoint / 'manifest.json').read_text())
        check_file(checkpoint / 'evidence-files.json', manifest['files']['evidence-files.json'])
        record = manifest['files'][item['archive']]
        if not same(record, item['identity']):
            raise ValueError('anchor manifest mismatch')
        anchors[name] = (checkpoint / item['archive'], index(checkpoint / 'evidence-files.json'), record)
    entries = {}
    for name, item in data['checkpoints'].items():
        checkpoint = inside(root, item['checkpoint'])
        check_file(checkpoint / 'manifest.json', item['manifest'])
        manifest = json.loads((checkpoint / 'manifest.json').read_text())
        check_file(checkpoint / 'evidence-files.json', manifest['files']['evidence-files.json'])
        if not same(manifest['files'][item['removed_archive']['name']], item['removed_archive']):
            raise ValueError('removed archive identity mismatch')
        old = index(checkpoint / 'evidence-files.json')
        anchor = anchors[item['anchor']][1]
        missing = {p for p, v in old.items() if p not in anchor or not same(v, anchor[p])}
        if missing != set(item['overrides']):
            raise ValueError('historical overrides incomplete')
        for p, v in item['overrides'].items():
            raw = base64.b64decode(v['base64'], validate=True)
            if len(raw) != old[p]['bytes'] or hashlib.sha256(raw).hexdigest() != old[p]['sha256']:
                raise ValueError('historical override mismatch')
        entries[name] = (item, old)
    return data, anchors, entries


def verify_archive(path, expected, record):
    check_file(path, record)
    seen = set()
    with tarfile.open(path, 'r|gz') as tf:
        for m in tf:
            p = member_name(m.name)
            if not m.isfile() or p in seen or p not in expected:
                raise ValueError('unexpected or duplicate archive member: ' + p)
            h = hashlib.sha256()
            with tf.extractfile(m) as stream:
                for block in iter(lambda: stream.read(1024 * 1024), b''):
                    h.update(block)
            if m.size != expected[p]['bytes'] or h.hexdigest() != expected[p]['sha256']:
                raise ValueError('archive member mismatch: ' + p)
            seen.add(p)
    if seen != set(expected):
        raise ValueError('incomplete anchor archive')
    return len(seen)


def extract(catalog, checkpoint, destination, members=None, root=ROOT):
    data, anchors, entries = load(catalog, root)
    item, old = entries[checkpoint]
    wanted = set(old) if members is None else {member_name(p) for p in members}
    if not wanted or not wanted <= old.keys():
        raise ValueError('unknown or empty member selection')
    destination = Path(destination).resolve()
    if destination.exists():
        raise ValueError('extraction requires a new destination')
    destination.mkdir(parents=True)
    remaining = set(wanted)

    def write(p, raw):
        if len(raw) != old[p]['bytes'] or hashlib.sha256(raw).hexdigest() != old[p]['sha256']:
            raise ValueError('extracted member mismatch: ' + p)
        target = inside(destination, p)
        target.parent.mkdir(parents=True, exist_ok=True)
        with target.open('xb') as f:
            f.write(raw)
        remaining.remove(p)

    for p in wanted & item['overrides'].keys():
        write(p, base64.b64decode(item['overrides'][p]['base64'], validate=True))
    if remaining:
        archive, _, record = anchors[item['anchor']]
        check_file(archive, record)
        with tarfile.open(archive, 'r|gz') as tf:
            for m in tf:
                p = member_name(m.name)
                if p not in remaining:
                    continue
                if not m.isfile():
                    raise ValueError('non-file member')
                write(p, tf.extractfile(m).read())
                if not remaining:
                    break
    if remaining:
        raise ValueError('missing requested evidence')
    return len(wanted)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=('verify', 'extract'))
    parser.add_argument('--catalog', type=Path, default=DEFAULT)
    parser.add_argument('--checkpoint')
    parser.add_argument('--dest', type=Path)
    parser.add_argument('--member', action='append')
    args = parser.parse_args()
    if args.action == 'verify':
        _, anchors, entries = load(args.catalog)
        for name, (archive, expected, record) in anchors.items():
            print('Verified anchor', name, verify_archive(archive, expected, record), 'members', flush=True)
        print('Verified logical restoration maps for', len(entries), 'historical checkpoints')
    else:
        if not args.checkpoint or not args.dest:
            parser.error('extract requires --checkpoint and --dest')
        print('Extracted', extract(args.catalog, args.checkpoint, args.dest, args.member), 'verified members')


if __name__ == '__main__':
    main()
