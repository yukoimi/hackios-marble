#!/usr/bin/env python3
"""Normalize a private Linux build copy of a Windows source snapshot to LF."""
from pathlib import Path
import argparse

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('directory', type=Path, nargs='?', default=Path(__file__).resolve().parents[1])
root = p.parse_args().directory.resolve()
count = 0
for path in root.rglob('*'):
    if not path.is_file() or path.is_symlink():
        continue
    if any(part in ('.git', 'out', 'prebuilt', 'xnu-src') for part in path.relative_to(root).parts):
        continue
    data = path.read_bytes()
    if b'\0' in data or b'\r\n' not in data:
        continue
    try:
        data.decode('utf8')
    except UnicodeError:
        continue
    path.write_bytes(data.replace(b'\r\n', b'\n'))
    count += 1
print('Normalized', count, 'source files in', root)
