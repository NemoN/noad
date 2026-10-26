#!/usr/bin/env python3
"""Compare noad's actual output against a frame-based regression reference."""
import argparse
import json
from pathlib import Path
import re

MARK = re.compile(r'^(\d+):(\d{2}):(\d{2})\.(\d+)\b')


def frames(text, fps):
    if fps not in (25, 50):
        raise ValueError('This regression tool supports 25 and 50 fps recordings')
    result = []
    for line in text.splitlines():
        if not line.strip():
            continue
        match = MARK.match(line)
        if not match:
            raise ValueError('Malformed noad mark: ' + line)
        hour, minute, second, frame = map(int, match.groups())
        if minute >= 60 or second >= 60 or frame >= fps:
            raise ValueError('Out-of-range noad mark: ' + line)
        position = (hour * 3600 + minute * 60 + second) * fps + frame
        if result and position <= result[-1]:
            raise ValueError('Marks must be strictly increasing')
        result.append(position)
    if not result or len(result) % 2:
        raise ValueError('Expected complete pairs of programme keep marks')
    return result


def compare(expected, actual, fps, tolerance=0):
    reference, observed = frames(expected, fps), frames(actual, fps)
    same_count = len(reference) == len(observed)
    deltas = [b - a for a, b in zip(reference, observed)]
    return {'matches': same_count and all(abs(n) <= tolerance for n in deltas),
            'expected_marks': len(reference), 'actual_marks': len(observed),
            'delta_frames': deltas,
            'maximum_absolute_delta_frames': max(map(abs, deltas), default=0)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('expected', type=Path)
    parser.add_argument('actual', type=Path)
    parser.add_argument('--fps', type=int, required=True, choices=[25, 50])
    parser.add_argument('--tolerance-frames', type=int, default=0)
    args = parser.parse_args()
    if args.tolerance_frames < 0:
        parser.error('tolerance must not be negative')
    result = compare(args.expected.read_text(), args.actual.read_text(), args.fps, args.tolerance_frames)
    print(json.dumps(result))
    return 0 if result['matches'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
