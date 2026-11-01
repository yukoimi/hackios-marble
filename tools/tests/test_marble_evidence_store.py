"""Lossless historical restoration and rejection of unsafe/incomplete maps."""
import base64
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import tarfile
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('store', Path(__file__).parents[1] / 'marble-evidence-store.py')
store = importlib.util.module_from_spec(spec)
spec.loader.exec_module(store)


class EvidenceStore(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.old = {'out/shared.log': b'old evidence', 'out/hardware-status.json': b'FAILED old state'}
        current = dict(self.old, **{'out/hardware-status.json': b'FAILED newer state'})
        def identity(raw):
            return dict(bytes=len(raw), sha256=hashlib.sha256(raw).hexdigest())
        def write(path, raw):
            path.write_bytes(raw)
            return identity(raw)
        for name, contents in [('base', current), ('old', self.old)]:
            d = self.root / name
            d.mkdir()
            members = {p: identity(v) for p, v in contents.items()}
            archive = d / 'evidence.tar.gz'
            with tarfile.open(archive, 'w:gz') as tf:
                for p, raw in contents.items():
                    m = tarfile.TarInfo(p)
                    m.size = len(raw)
                    tf.addfile(m, io.BytesIO(raw))
            files = {'evidence.tar.gz': identity(archive.read_bytes()),
                     'evidence-files.json': write(d / 'evidence-files.json', json.dumps(members).encode())}
            write(d / 'manifest.json', json.dumps(dict(files=files)).encode())
        anchor = self.root / 'base'
        old = self.root / 'old'
        anchor_record = json.loads((anchor / 'manifest.json').read_text())['files']['evidence.tar.gz']
        old_record = json.loads((old / 'manifest.json').read_text())['files']['evidence.tar.gz']
        self.catalog = self.root / 'catalog.json'
        self.data = dict(schema='marble-evidence-storage-v1', anchors=dict(base=dict(checkpoint='base',
            archive='evidence.tar.gz', manifest=identity((anchor/'manifest.json').read_bytes()), identity=anchor_record)),
            checkpoints=dict(old=dict(checkpoint='old', manifest=identity((old/'manifest.json').read_bytes()),
            removed_archive=dict(name='evidence.tar.gz', **old_record), anchor='base',
            overrides={'out/hardware-status.json': dict(base64=base64.b64encode(self.old['out/hardware-status.json']).decode())})))
        self.save()
        (old / 'evidence.tar.gz').unlink()

    def save(self):
        self.catalog.write_text(json.dumps(self.data))

    def test_restores_original_status_and_shared_evidence_after_deletion(self):
        dest = self.root / 'restored'
        self.assertEqual(store.extract(self.catalog, 'old', dest, root=self.root), 2)
        for p, raw in self.old.items():
            self.assertEqual((dest / p).read_bytes(), raw)

    def test_missing_or_corrupted_historical_state_is_rejected(self):
        for overrides in ({}, {'out/hardware-status.json': dict(base64=base64.b64encode(b'PASS').decode())}):
            self.data['checkpoints']['old']['overrides'] = overrides
            self.save()
            with self.assertRaises(ValueError):
                store.load(self.catalog, self.root)

    def test_changed_manifest_is_rejected(self):
        (self.root/'old/manifest.json').write_text('{}')
        with self.assertRaises(ValueError):
            store.load(self.catalog, self.root)

    def test_anchor_actual_bytes_are_verified(self):
        _, anchors, _ = store.load(self.catalog, self.root)
        path, expected, record = anchors['base']
        self.assertEqual(store.verify_archive(path, expected, record), 2)
        path.write_bytes(b'bad archive')
        with self.assertRaises(ValueError):
            store.verify_archive(path, expected, record)

    def test_does_not_overwrite_live_status(self):
        dest = self.root/'live'
        dest.mkdir()
        with self.assertRaises(ValueError):
            store.extract(self.catalog, 'old', dest, root=self.root)

    def test_traversal_and_unknown_member_rejected_before_creation(self):
        for name in ('../escape', '/absolute', 'C:/escape', 'out/unknown'):
            with self.assertRaises(ValueError):
                store.extract(self.catalog, 'old', self.root/'new', [name], self.root)
            self.assertFalse((self.root/'new').exists())


if __name__ == '__main__':
    unittest.main()
