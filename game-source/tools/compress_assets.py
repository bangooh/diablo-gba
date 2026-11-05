from pathlib import Path
import json,struct,sys
from lz77 import compress,decompress,indexed
root=Path(sys.argv[1]);report=[]
for p in sorted(root.glob('*.bin')):
 raw=p.read_bytes();size=4096 if p.stem=='prop_frames' else 1024 if p.stem.startswith('control_globes_') else 128 if p.stem=='town_tiles' else 2048 if p.stem.startswith(('warrior_','zombie_','skeleton_','npc_','fallen_spear_','fallen_sword_','scavenger_','captain_')) and not p.stem.endswith('palette') else 0
 if size:
  packed=indexed(raw,size);n=struct.unpack_from('<I',packed)[0]
  for i in range(n):
   off=struct.unpack_from('<I',packed,4+4*i)[0];assert decompress(packed[off:])==raw[i*size:(i+1)*size]
 else:packed=compress(raw);assert decompress(packed)==raw
 p.with_suffix('.lz').write_bytes(packed);report.append({'asset':p.stem,'raw':len(raw),'compressed':len(packed),'block_size':size})
header=['#ifndef PACKED_ASSETS_H','#define PACKED_ASSETS_H','#include <stdint.h>']
for r in report:header += [f'extern const uint8_t asset_{r["asset"]}[];',f'#define RAW_{r["asset"].upper()} {r["raw"]}']
header += ['#endif'];(root/'packed_assets.h').write_text('\n'.join(header)+'\n')
summary={'raw_bytes':sum(r['raw'] for r in report),'compressed_bytes':sum(r['compressed'] for r in report),'codec':'GBA LZ77 0x10; indexed tile and animation blocks','assets':report}
(root/'compression-report.json').write_text(json.dumps(summary,indent=2));print({k:v for k,v in summary.items() if k!='assets'})
