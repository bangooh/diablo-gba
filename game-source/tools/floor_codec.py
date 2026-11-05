"""Lossless sparse decals: shared small palettes and bit-packed literal runs."""
import struct
from functools import lru_cache

def encode(pictures, translations=None):
    # Merge compatible colour sets without increasing any frame's bit width.
    sets = {frozenset(p) - {0} for p in pictures}
    bins = []; selected = {}
    for colors in sorted(sets, key=lambda s: (-len(s), tuple(sorted(s)))):
        bits = max(1, (len(colors) - 1).bit_length())
        candidates = [(len(colors - palette), i) for i, (width, palette) in enumerate(bins)
                      if width == bits and len(colors | palette) <= 1 << bits]
        if candidates:
            _, index = min(candidates); bins[index][1].update(colors)
        else:
            index = len(bins); bins.append((bits, set(colors)))
        selected[colors] = index
    palettes = [bytes(sorted(p)) for _, p in bins]
    mappings = [{v: i for i, v in enumerate(p)} for p in palettes]
    blob = bytearray(4 + 4 * len(pictures)); struct.pack_into('<I', blob, 0, len(pictures))
    palette_offsets = []
    for palette in palettes:
        palette_offsets.append(len(blob)); blob.extend(palette)

    @lru_cache(maxsize=None)
    def row_code(row, bits):
        cost = [10000] * 65; cost[64] = 0; choice = [None] * 64
        for x in range(63, -1, -1):
            if row[x] == 255:
                end = x + 1
                while end < 64 and row[end] == 255: end += 1
                cost[x] = 1 + cost[end]; choice[x] = (end, 0); continue
            end = x
            while end < 64 and row[end] != 255:
                end += 1; value = 1 + ((end-x)*bits+7)//8 + cost[end]
                if value < cost[x]: cost[x] = value; choice[x] = (end, 1)
            end = x + 1
            while end < 64 and end-x < 63 and row[end] == row[x]: end += 1
            for stop in range(x+2, end+1):
                value = 2 + cost[stop]
                if value < cost[x]: cost[x] = value; choice[x] = (stop, 2)
        out = bytearray(); x = 0
        while x < 64:
            end, kind = choice[x]; size = end-x
            if kind == 0: out.append(256-size)
            elif kind == 2: out.extend((64+size, row[x]))
            else:
                out.append(size)
                packed = sum(v << (i*bits) for i, v in enumerate(row[x:end]))
                out.extend(packed.to_bytes((size*bits+7)//8, 'little'))
            x = end
        return bytes(out) + b'\0'

    unique = {}; bodies={}; translated_palettes={}
    for i, pixels in enumerate(pictures):
        colors = frozenset(pixels) - {0}; index = selected[colors]
        bits = bins[index][0]; mapping = mappings[index]
        occupied = [y for y in range(64) if any(pixels[y*64:y*64+64])]
        top = min(occupied) if occupied else 0; height = max(occupied)+1-top if occupied else 0
        data = bytearray(); offsets = []
        for y in range(top, top+height):
            offsets.append(8+height*2+len(data))
            data.extend(row_code(bytes(mapping[v] if v else 255 for v in pixels[y*64:y*64+64]), bits))
        if translations is not None:
            visible=bytes(translations[i][v] for v in palettes[index])
            if visible not in translated_palettes:translated_palettes[visible]=len(blob);blob.extend(visible)
            palette_offset=translated_palettes[visible]
        else:palette_offset=palette_offsets[index]
        body=struct.pack('<'+'H'*height,*offsets)+data
        if body not in bodies:
            blob.extend(bytes(-len(blob)%4));bodies[body]=len(blob);blob.extend(body)
        frame = struct.pack('<4BII', top, height, bits, 2, palette_offset,bodies[body])
        if frame not in unique:
            blob.extend(bytes(-len(blob)%4)); unique[frame] = len(blob); blob.extend(frame)
        struct.pack_into('<I', blob, 4+i*4, unique[frame])
    return bytes(blob), {'palette_count': len(palettes), 'palette_bytes': sum(map(len, palettes)), 'unique_frames': len(unique)}
