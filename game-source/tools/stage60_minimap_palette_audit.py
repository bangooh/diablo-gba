"""Read-only complete scene-palette/gamma choice audit for approved map B."""
from pathlib import Path
import hashlib, json, re, struct, sys
root=Path(sys.argv[1]).resolve()
out=Path(sys.argv[2]).resolve()
out.mkdir(parents=True,exist_ok=True)
def lz_decode(data):
    assert data[0]==0x10
    size=int.from_bytes(data[1:4],'little'); result=bytearray(); p=4
    while len(result)<size:
        flags=data[p];p+=1
        for bit in range(7,-1,-1):
            if len(result)>=size: break
            if flags&(1<<bit):
                a,b=data[p:p+2];p+=2;n=(a>>4)+3;distance=((a&15)<<8|b)+1
                for _ in range(n):result.append(result[-distance])
            else:result.append(data[p]);p+=1
    assert len(result)==size
    return bytes(result)
def rgb5(c): return [c&31,(c>>5)&31,(c>>10)&31]
def rgb8(c): return [(x<<3)|(x>>2) for x in rgb5(c)]
def gamma(c,g): return sum(min(31,x*(8+g)//10)<<(5*i) for i,x in enumerate(rgb5(c)))
slots=list(map(int,re.search(r'bg_static_slots\[32\]=\{([^}]+)\}',(root/'generated/bg_water_palette.h').read_text())[1].split(',')))
water=list(map(int,re.search(r'bg_water_slots\[30\]=\{([^}]+)\}',(root/'generated/bg_water_palette.h').read_text())[1].split(',')))
assert all(not(slots[i>>3]>>(i&7)&1) for i in water)
def select(palette,target,town):
    return min((i for i in range(1,256) if i%16 and(town or slots[i>>3]>>(i&7)&1)),key=lambda i:sum((a-b)**2 for a,b in zip(rgb5(palette[i]),target)))
scenes=[]
for name,asset,town in [('town','inventory_palette.lz',True),('dungeon-1-through-16','bg_palette.lz',False)]:
    packed=(root/'generated'/asset).read_bytes();raw=lz_decode(packed);assert len(raw)==512
    pal=list(struct.unpack('<256H',raw));old=select(pal,(26,23,14),town);new=select(pal,(16,15,10),town)
    assert new not in water or town
    if not town:assert new==199 and rgb5(pal[new])==[16,15,10]
    levels=[]
    for level in range(10):
        oc,nc=gamma(pal[old],level),gamma(pal[new],level)
        assert all(n<=o for o,n in zip(rgb5(oc),rgb5(nc)))
        levels.append({'gammaStored':level,'menuLevel':level+1,'oldRgb5':rgb5(oc),'approvedRgb5':rgb5(nc),'approvedRgb8':rgb8(nc)})
    scenes.append({'name':name,'asset':asset,'assetSha256':hashlib.sha256(packed).hexdigest(),'oldIndex':old,'approvedIndex':new,'oldRgb5':rgb5(pal[old]),'approvedRgb5':rgb5(pal[new]),'approvedRgb8':rgb8(pal[new]),'gammaLevels':levels})
result={'result':'PASS','approvedTargetRgb5':[16,15,10],'scenePalettes':scenes,'allDungeonWaterStates':34,'mapIndexUnaffectedByWater':True,'paletteMutationRequired':False,'method':'Exact target selection over decoded shipping assets, same nontransparent/static slot filtering and gamma integer arithmetic as load_scene/apply_gamma. Dungeon levels share one BG palette; all 34 water states only touch excluded nonstatic slots. This is read-only math and asset verification; native final ROM gate is separate.'}
(out/'palette-audit.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps({'result':'PASS','scenes':[{k:s[k] for k in ['name','oldIndex','approvedIndex','approvedRgb5','approvedRgb8']} for s in scenes]}))
