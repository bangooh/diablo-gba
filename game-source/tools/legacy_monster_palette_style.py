"""User-requested white-highlight correction for Obsidian Lord and Carver.

Applied after native import or palette sharing, without touching sprite indices,
source TRNs, immutable archives or any unrelated palette. Match explicit RGB555
colors within each bank rather than quantizer-dependent palette indices.
"""
from pathlib import Path
import argparse, hashlib, json, struct
from lz77 import compress, decompress
ROOT=Path(__file__).resolve().parents[1]
OVERRIDES={
 'obsidian_lord':[((26,26,26),(9,9,10)),((27,27,27),(13,13,14))],
 'devil_kin_spear':[((31,31,31),(6,8,14)),((31,31,31),(11,12,17))],
}
def rgb555(c):return c[0]|(c[1]<<5)|(c[2]<<10)
def style_palette(key,raw):
    if key not in OVERRIDES:return raw
    assert len(raw)==64,key+': expected normal+selected16-color banks'
    colors=list(struct.unpack('<32H',raw));assert colors[0]==colors[16]==0
    for bank,(old,new) in enumerate(OVERRIDES[key]):
        before,after=rgb555(old),rgb555(new);indices=range(bank*16+1,bank*16+16)
        assert any(colors[i] in (before,after) for i in indices),key+': palette changed; review highlight override'
        for i in indices:
            if colors[i]==before:colors[i]=after
    return struct.pack('<32H',*colors)
def style_assets(assets):
    """Modify a filename->compressed-bytes bundle before its final hash report."""
    for key in OVERRIDES:
        name=key+'_palette.lz'
        if name not in assets:continue
        old=assets[name];raw=decompress(old);styled=style_palette(key,raw)
        if styled!=raw:assets[name]=compress(styled)
def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--generated',type=Path,default=ROOT/'generated');ap.add_argument('--check',action='store_true');args=ap.parse_args();records=[]
    for key in OVERRIDES:
        path=args.generated/(key+'_palette.lz');old=path.read_bytes();raw=decompress(old);styled=style_palette(key,raw)
        if args.check:assert raw==styled,key+': unapplied palette style'
        elif raw!=styled:path.write_bytes(compress(styled))
        records.append({'key':key,'changed_entries':[i for i,(a,b) in enumerate(zip(struct.unpack('<32H',raw),struct.unpack('<32H',styled))) if a!=b],'bytes':path.stat().st_size,'sha256':hashlib.sha256(path.read_bytes()).hexdigest()})
    print(json.dumps({'check':args.check,'palettes':records},indent=2))
if __name__=='__main__':main()
