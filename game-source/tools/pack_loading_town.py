"""Pack native Cutstart town loading art, retaining the existing asset symbols.

The user-owned DIABDAT.MPQ provides Gendata/Cutstart.CEL and Cutstart.pal.
Pinned Devilution InitCutscene selects these for NEWGAME and LOADGAME;
ordinary returns to town select the already packaged Cuttt instead.
"""
from pathlib import Path
import argparse
import hashlib
import json
import struct
from PIL import Image
from lz77 import compress

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "source_assets/loading_stage54"
OUT = ROOT / "generated"


def decode_cel(raw):
    assert len(raw) >= 12
    count, first, end = struct.unpack_from("<3I", raw)
    assert count == 1 and first == 12 and end == len(raw)
    decoded = bytearray()
    pos = first
    while pos < end:
        run = raw[pos]
        pos += 1
        assert run != 0
        if run >= 128:
            decoded.extend(bytes(256 - run))
        else:
            assert pos + run <= end
            decoded.extend(raw[pos:pos + run])
            pos += run
        assert len(decoded) <= 640 * 480
    assert len(decoded) == 640 * 480
    return bytes(decoded)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mpq", nargs="?", help="optionally refresh exact original source files")
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    if args.mpq:
        import mpyq
        from extract_assets import read_mpq
        archive = mpyq.MPQArchive(args.mpq, listfile=False)
        SOURCE.mkdir(parents=True, exist_ok=True)
        for extension in ("CEL", "pal"):
            data = read_mpq(archive, "Gendata\\Cutstart." + extension)
            target = SOURCE / ("Cutstart." + extension)
            if args.check:
                assert target.read_bytes() == data, str(target)
            else:
                target.write_bytes(data)
    raw = (SOURCE / "Cutstart.CEL").read_bytes()
    pal = (SOURCE / "Cutstart.pal").read_bytes()
    assert len(pal) == 768
    image = Image.frombytes("P", (640, 480), decode_cel(raw)).transpose(Image.Transpose.FLIP_TOP_BOTTOM)
    image.putpalette(pal)
    small = image.resize((240, 160), Image.Resampling.NEAREST)
    pixels = small.tobytes()
    # Check the nearest-pixel resampling with a separate direct source lookup.
    source = image.tobytes()
    # Pillow advances the source-center coordinate iteratively in double
    # precision. Preserve its tie behavior at this non-integer horizontal
    # ratio, matching the existing native region-art packer.
    xs = []
    center = (640 / 240) / 2
    for x in range(240):
        xs.append(int(center))
        center += 640 / 240
    expected = bytes(source[(y*3+1)*640 + xs[x]]
                     for y in range(160) for x in range(240))
    assert pixels == expected
    colors = [(pal[i] >> 3) | ((pal[i+1] >> 3) << 5) | ((pal[i+2] >> 3) << 10)
              for i in range(0, 768, 3)]
    assets = {"loading_tristram": pixels, "loading_tristram_palette": struct.pack("<256H", *colors)}
    generated = {}
    for name, data in assets.items():
        generated[name + ".bin"] = data
        generated[name + ".lz"] = compress(data)
    generated["loading_town.h"] = ("#pragma once\n"
        "/* Native Gendata/Cutstart; legacy symbol names retained. */\n"
        "extern const uint8_t asset_loading_tristram[],asset_loading_tristram_palette[];\n"
        "#define LOADING_TOWN_COLOR 43\n").encode()
    report = {"source": "Gendata\\Cutstart.CEL + Gendata\\Cutstart.pal", "source_size": [640, 480],
              "output_size": [240, 160], "resampling": "nearest original indexed pixels; no generated art or dithering",
              "cel_sha256": hashlib.sha256(raw).hexdigest(), "palette_sha256": hashlib.sha256(pal).hexdigest(),
              "indexed_sha256": hashlib.sha256(pixels).hexdigest(), "progress_color_index": 43,
              "packed_bytes": sum(len(data) for name, data in generated.items() if name.endswith(".lz")),
              "runtime_ram_delta": 0, "legacy_symbol": "asset_loading_tristram",
              "verification": "strict native CEL bounds and independent nearest-pixel oracle"}
    generated["loading-town-native-report.json"] = (json.dumps(report, indent=2) + "\n").encode()
    for name, data in generated.items():
        target = OUT / name
        if args.check:
            assert target.read_bytes() == data, str(target)
        else:
            target.write_bytes(data)
    if not args.check:
        small.save(SOURCE / "Cutstart.png")
    print(json.dumps({"result": "PASS", "check_only": args.check, **report}, indent=2))


if __name__ == "__main__":
    main()
