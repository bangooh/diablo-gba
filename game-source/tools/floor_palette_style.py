"""Scoped Stage37 death/corpse highlight correction; native sources stay intact.

The post-encode hook only changes exclusively owned palette bytes.
Every original compressed body is retained, including shared unique geometry.
"""
import hashlib
import json
import struct
from pathlib import Path

STYLES = {26: (7, 7, 8), 39: (5, 6, 11)}  # living tones at corpse's existing .80 level
REPORT_NAME = 'floor-palette-style-report.json'


def sha(data):
    return hashlib.sha256(data).hexdigest()


def rgb(word):
    return tuple((word >> shift) & 31 for shift in (0, 5, 10))


def palette_size(blob, header):
    """Read the actual used indices; palette lengths are implicit in v4."""
    _, height, bits, version, _, body = struct.unpack_from('<4BII', blob, header)
    assert version == 2 and 1 <= bits <= 8
    mask, largest = (1 << bits) - 1, -1
    for row in range(height):
        offset = body + struct.unpack_from('<H', blob, body + 2 * row)[0] - 8
        while blob[offset]:
            run = blob[offset]
            offset += 1
            if run >= 128:
                continue
            repeat = run > 64
            count = run - 64 if repeat else run
            for index in range(count):
                bit = index * bits
                value = blob[offset] if repeat else blob[offset + bit // 8] >> (bit & 7)
                if not repeat and (bit & 7) + bits > 8:
                    value |= blob[offset + bit // 8 + 1] << (8 - (bit & 7))
                largest = max(largest, value & mask)
            offset += 1 if repeat else (count * bits + 7) // 8
    return largest + 1


def apply(blob, bg_words, bases, counts):
    """Edit only palette bytes exclusively referenced by the requested kinds."""
    old_color = [i for i, word in enumerate(bg_words) if rgb(word) == (25, 25, 25)]
    assert old_color == [242], 'unexpected original highlight in BG palette'
    colors = list(map(rgb, bg_words))
    targets, kinds = {}, []
    for kind, tone in STYLES.items():
        target = min(range(1, 256), key=lambda i: sum((colors[i][c] - tone[c]) ** 2 for c in range(3)))
        assert target == {26: 9, 39: 187}[kind], 'BG palette changed; review corpse style'
        frames = list(range(kind * 8, kind * 8 + 8)) + list(range(bases[kind], bases[kind] + counts[kind] * 8))
        for frame in frames:
            targets[frame] = target
        kinds.append({'kind': kind, 'frames': len(frames),
                      'source_bg_index': 242, 'target_bg_index': target,
                      'requested_corpse_rgb555': tone, 'actual_bg_rgb555': colors[target]})
    # Check every reference, including palette sharing with unique monsters.
    # Never change a byte that an unrelated frame can read. Ownership checks
    # run offline and add no code/data cost to the GBA renderer.
    owners, changes, sizes = {}, {}, {}
    for frame in range(struct.unpack_from('<I', blob)[0]):
        header = struct.unpack_from('<I', blob, 4 + frame * 4)[0]
        palette = struct.unpack_from('<I', blob, header + 4)[0]
        if header not in sizes:
            sizes[header] = palette_size(blob, header)
        for offset in range(palette, palette + sizes[header]):
            owners.setdefault(offset, set()).add(frame)
            if frame in targets and blob[offset] == 242:
                changes.setdefault(offset, set()).add(targets[frame])
    result, original_bytes = bytearray(blob), []
    for offset, values in sorted(changes.items()):
        assert len(values) == 1, 'two styles share a palette byte; clone palette first'
        target = next(iter(values))
        assert all(targets.get(frame) == target for frame in owners[offset]), \
            'palette is shared with an unrelated frame; clone palette first'
        original_bytes.append([offset, blob[offset]])
        result[offset] = target
    result = bytes(result)
    report = {'operation': 'scoped corpse/death highlight palette correction',
              'source_sha256': sha(blob), 'source_bytes': len(blob),
              'sha256': sha(result), 'encoded_bytes': len(result),
              'added_bytes': 0, 'kinds': kinds,
              'original_palette_bytes': original_bytes,
              'all_palette_reference_owners_checked': True,
              'shared_bg_palette_changed': False, 'compressed_bodies_changed': False}
    return result, report


def restore_baseline(blob, report):
    """Historical validators verify the preserved base layer independently."""
    assert sha(blob) == report['sha256'] and len(blob) == report['encoded_bytes']
    old = bytearray(blob[:report['source_bytes']])
    for offset, value in report['original_palette_bytes']:
        old[offset] = value
    assert sha(old) == report['source_sha256'], 'floor geometry or baseline bytes changed'
    return bytes(old)


def write_report(directory, report):
    (Path(directory) / REPORT_NAME).write_text(json.dumps(report, indent=2) + '\n')
