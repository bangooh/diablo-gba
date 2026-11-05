"""Restore compact storage for special actors without changing decoded pixels."""
import hashlib
import json
from pathlib import Path
import struct
import tempfile
from pack_hot_monsters import Decoder, raw_tiles

OUT = Path(__file__).resolve().parents[1] / 'generated'


def count(data):
    first = struct.unpack_from('<I', data)[0]
    return struct.unpack_from('<I', data, 4)[0] if first in (0x31525053, 0x32525053) else first


def main():
    rows = []
    with tempfile.TemporaryDirectory() as temp:
        decoder = Decoder(temp, (OUT / 'hero_frame_pool.lz').read_bytes())
        for name, expected in (('advocate_s', 160), ('skeleton_king_s', 48)):
            path = OUT / (name + '.lz')
            original = path.read_bytes()
            total = count(original)
            assert total == expected or (name == 'skeleton_king_s' and total == 96), (name, total)
            # Legacy king uses the first contiguous 48 frames, not six per direction.
            frames = decoder.frames(original, expected)
            encoded = raw_tiles(frames)
            assert decoder.frames(encoded, expected) == frames
            path.write_bytes(encoded)
            rows.append(dict(asset=name, frames=expected, bytes=len(encoded),
                             sha256=hashlib.sha256(encoded).hexdigest()))
        for suffix, total in (('h', 48), ('s', 128)):
            path = OUT / f'm71_44_{suffix}.lz'
            alias = (OUT / f'diablo_{suffix}.lz').read_bytes()
            assert count(path.read_bytes()) == count(alias) == total
            assert decoder.frames(path.read_bytes(), total) == decoder.frames(alias, total)
            path.write_bytes(alias)
            rows.append(dict(asset=path.stem, frames=total, bytes=len(alias),
                             sha256=hashlib.sha256(alias).hexdigest()))
    (OUT / 'actor-storage-finalization.json').write_text(json.dumps(rows, indent=2) + '\n')
    print('PASS: four special actor streams, decoded equality and buffer guards')


if __name__ == '__main__':
    main()
