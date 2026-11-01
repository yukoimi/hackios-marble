"""Measure timestamps in a selected source/composition interval, not optical scanout."""
import argparse
import json
import math
import struct
from pathlib import Path


def timestamps(path):
    with path.open('rb') as f:
        header = f.read(32)
        if len(header) != 32:
            raise ValueError('truncated header')
        magic, width, height, count, oldest, began = struct.unpack('<8sIIIIQ', header)
        if (magic not in (b'RWREC003', b'RWREC004') or not 1 <= width <= 2048 or
                not 1 <= height <= 4096 or not count or oldest >= count):
            raise ValueError('invalid recording header')
        stride = 8 + width * height * 3
        if path.stat().st_size != 32 + count * stride:
            raise ValueError('truncated or extra frame bytes')
        values = []
        for index in list(range(oldest, count)) + list(range(oldest)):
            f.seek(32 + index * stride)
            values.append((began + struct.unpack('<Q', f.read(8))[0]) / 1e9)
        if any(a >= b for a, b in zip(values, values[1:])):
            raise ValueError('nonmonotonic recording')
        return values


def percentile(values, fraction):
    ordered = sorted(values)
    return ordered[max(0, math.ceil(len(ordered) * fraction) - 1)]


def recording_scope(path):
    with path.open('rb') as f:
        magic = f.read(8)
    if magic == b'RWREC004':
        return 'completed multi-layer composition under mutex; not optical scanout or proof of animation throughout interval'
    if magic == b'RWREC003':
        return 'bottom-layer source recording; not scanout, optical refresh, or proof of animation throughout interval'
    raise ValueError('invalid recording format')


def measure(values, first, last, hz, scope='bottom-layer source recording; not scanout, optical refresh, or proof of animation throughout interval'):
    selected = [(i, t) for i, t in enumerate(values) if first <= t <= last]
    if len(selected) < 2:
        raise ValueError('interval must contain at least two retained frames')
    gaps = [b[1] - a[1] for a, b in zip(selected, selected[1:])]
    period = 1 / hz
    return {
        'scope': scope,
        'requested_seconds': [first, last],
        'observed_seconds': [selected[0][1], selected[-1][1]],
        'frame_indices': [selected[0][0], selected[-1][0]],
        'frames': len(selected),
        'intervals': len(gaps),
        'observed_fps': (len(selected) - 1) / (selected[-1][1] - selected[0][1]),
        'median_ms': 1000 * percentile(gaps, .5),
        'p95_ms': 1000 * percentile(gaps, .95),
        'max_ms': 1000 * max(gaps),
        'intervals_over_1_5_refresh_periods': sum(g > 1.5 * period for g in gaps),
        'reference_hz': hz,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('recording', type=Path)
    parser.add_argument('--start', type=float, required=True, help='kernel monotonic seconds')
    parser.add_argument('--end', type=float, required=True, help='kernel monotonic seconds')
    parser.add_argument('--hz', type=float, default=60)
    args = parser.parse_args()
    if not all(map(math.isfinite, (args.start, args.end, args.hz))) or args.end <= args.start or args.hz <= 0:
        parser.error('invalid interval or refresh rate')
    print(json.dumps(measure(timestamps(args.recording), args.start, args.end, args.hz,
                             recording_scope(args.recording)), indent=2))


if __name__ == '__main__':
    main()
