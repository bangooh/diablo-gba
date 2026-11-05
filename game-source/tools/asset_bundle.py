"""Emit referenced immutable asset aliases without changing any packed stream.

Keep all symbols referenced by compiled objects; identical bytes share an address.
PCM streams may also share equal zero suffixes/prefixes. Every complete original
stream, including its DMA guard, remains contiguous and four-byte aligned.
Hash comments also invalidate assemblers that do not track incbin dependencies.
"""
from pathlib import Path
import hashlib, struct


def referenced_assets(objects):
    """Read unresolved asset symbols from the actual little-endian ELF32 inputs."""
    result = set()
    for path in objects:
        data = Path(path).read_bytes()
        assert data[:7] == b'\x7fELF\x01\x01\x01', path
        offset = struct.unpack_from('<I', data, 32)[0]
        size, count = struct.unpack_from('<HH', data, 46)
        sections = [struct.unpack_from('<10I', data, offset+i*size) for i in range(count)]
        for section in sections:
            if section[1] != 2:
                continue
            strings = sections[section[6]]
            names = data[strings[4]:strings[4]+strings[5]]
            for pos in range(section[4], section[4]+section[5], section[9]):
                name, _, _, _, _, index = struct.unpack_from('<IIIBBH', data, pos)
                name = names[name:names.find(b'\0', name)].decode()
                if index == 0 and name.startswith('asset_'):
                    result.add(name)
    return result


def _pcm_order(streams):
    """Return (stream index, shared prefix length) in an acyclic physical order.

    Only raw audio_*.lz/front_audio.lz streams may overlap. Mixed PCM/non-PCM
    aliases retain ordinary whole-stream sharing but cannot join a zero chain.
    An unaligned predecessor cannot overlap: its following alignment padding
    is deliberately excluded from this byte-for-byte source-file comparison.
    """
    pcm = {}
    for index, (data, paths) in enumerate(streams):
        if all(p.suffix == '.lz' and
               (p.stem.startswith('audio_') or p.stem == 'front_audio')
               for p in paths):
            pcm[index] = ((len(data) - len(data.lstrip(b'\0'))) & ~3,
                          (len(data) - len(data.rstrip(b'\0'))) & ~3,
                          paths[0].stem)
    previous, following = {}, {}
    for index in sorted(pcm, key=lambda i: (-pcm[i][0], pcm[i][2])):
        head = pcm[index][0]
        if not head:
            continue
        choices = []
        for candidate, (_, tail, name) in pcm.items():
            if candidate == index or candidate in following or len(streams[candidate][0]) & 3:
                continue
            end = index
            while end in following:
                end = following[end]
            if end == candidate:
                continue
            choices.append((min(head, tail), tail, name, candidate))
        if choices:
            overlap, _, _, candidate = max(choices)
            if overlap:
                previous[index] = overlap
                following[candidate] = index
    order = []
    for index in range(len(streams)):
        if index in previous:
            continue
        order.append((index, 0))
        while index in following:
            index = following[index]
            order.append((index, previous[index]))
    assert len(order) == len(streams)
    return order


def emit_assets(directory, output, required=None):
    groups = {}
    logical = 0
    omitted = 0
    found = set()
    for path in sorted(Path(directory).iterdir()):
        if path.suffix not in ('.lz', '.rle'):
            continue
        if required is not None and 'asset_'+path.stem not in required:
            omitted += 1
            continue
        found.add('asset_'+path.stem)
        data = path.read_bytes()
        logical += (len(data) + 3) & ~3
        groups.setdefault(data, []).append(path)
    if required is not None:
        assert found == required, 'Missing generated assets: '+str(sorted(required-found))
    lines = ['.section .rodata.assets,"a",%progbits']
    streams = list(groups.items())
    unique_bytes = sum((len(data) + 3) & ~3 for data in groups)
    overlap_bytes = overlap_links = 0
    for index, overlap in _pcm_order(streams):
        data, paths = streams[index]
        lines += ['// sha256 ' + hashlib.sha256(data).hexdigest(), '.balign 4']
        for path in paths:
            symbol = 'asset_' + path.stem
            lines += ['.global ' + symbol,
                      '.set ' + symbol + ', . - ' + str(overlap) if overlap else symbol + ':']
        if overlap:
            lines += ['// Shared identical PCM zeros; full source and DMA guard retained.']
            overlap_bytes += overlap
            overlap_links += 1
        if len(data) > overlap:
            suffix = ', ' + str(overlap) + ', ' + str(len(data) - overlap) if overlap else ''
            lines += ['.incbin "' + str(paths[0].resolve()) + '"' + suffix]
    # Make the physical object size match the aligned-byte report even when the
    # final stream is unaligned (or several zero-only streams share its tail).
    lines += ['.balign 4']
    stored = unique_bytes - overlap_bytes
    Path(output).write_text('\n'.join(lines) + '\n')
    return {'symbols': sum(map(len, groups.values())), 'streams': len(groups),
            'logical_aligned_bytes': logical, 'stored_aligned_bytes': stored,
            'saved_bytes': logical - stored, 'unreferenced_files_omitted': omitted,
            'whole_stream_saved_bytes': logical - unique_bytes,
            'pcm_zero_overlap_bytes': overlap_bytes, 'pcm_overlap_links': overlap_links}
