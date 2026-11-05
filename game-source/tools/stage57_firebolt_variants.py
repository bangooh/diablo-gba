#!/usr/bin/env python3
"""Write isolated diagnostic includes. Does not build or edit production source."""
import argparse,hashlib,json
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('output',type=Path);a=p.parse_args()
root=Path(__file__).resolve().parents[1]
original=(root/'src/lighting.inc').read_text()
loop=' for(unsigned i=0;i<MAX_ARROWS;i++)if(game_arrows[i].life){'
assert original.count(loop)==1
disabled=original.replace(loop,' /* Stage57 diagnosis only: omit projectile lights, preserve all other work. */\n if(0)'+loop,1)
site='  lighting_source(DUNGEON_ORIGIN+((a->x-a->y)*16)/256,72+((a->x+a->y)*8)/256-(ground?8:24),3,1);'
assert original.count(site)==1
filtered=original.replace(site,'''  /* Stage57 diagnosis only: apply the exact radius3 screen-external reject
   * before storing moving arrow sources in the window-cache key. */
  int lx=DUNGEON_ORIGIN+((a->x-a->y)*16)/256;
  int ly=72+((a->x+a->y)*8)/256-(ground?8:24);
  int screen_x=lx-cam_x,screen_y=ly-cam_y;
  if(screen_x+24<0||screen_x-24>=240||screen_y+15<0||screen_y-15>=160)continue;
  lighting_source(lx,ly,3,1);''',1)
report={'productionUnchanged':True,'originalSha256':hashlib.sha256(original.encode()).hexdigest(),'variants':{}}
for name,content in [('no-arrow-light',disabled),('visible-arrow-light',filtered)]:
    out=a.output/name;out.mkdir(parents=True,exist_ok=True);(out/'lighting.inc').write_text(content)
    report['variants'][name]={'path':str((out/'lighting.inc').resolve()),'sha256':hashlib.sha256(content.encode()).hexdigest()}
(a.output/'variants.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
