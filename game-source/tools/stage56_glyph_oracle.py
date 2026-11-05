from pathlib import Path
import subprocess,json,re,hashlib,sys
root=Path(__file__).resolve().parents[1];p=Path(sys.argv[1]).resolve() if len(sys.argv)>1 else root/'generated/stage56-validation/hud/oracles';p.mkdir(parents=True,exist_ok=True);base=root/'tests/stage56-hud-reference.c';candidate=root/'src/gba_mode0.c'
def function(s,signature):
 a=s.index(signature);z=s.index('{',a);d=1;e=z+1
 while d:d+=(s[e]=='{')-(s[e]=='}');e+=1
 return s[a:e]
a=base.read_text();b=candidate.read_text()
font=function(a,'static inline unsigned font_tile(')
f0=function(a,'static UI_FN void hud_text(').replace('hud_text','reference_text',1)
f1=function(b,'static UI_FN void hud_text(').replace('hud_text','candidate_text',1)
table=re.search(r'static const uint8_t hud_glyph_index\[256\]=\{[\s\S]*?\};',b).group()
letters=re.search(r'^#define HUD_GLYPHS "(.*)"$',(root/'generated/assets.h').read_text(),re.M).group(1)
s='#include <stdint.h>\n#include <stdio.h>\n#include <assert.h>\n#include <string.h>\n#define UI_FN\nstatic const char glyphs[]='+json.dumps(letters)+';\nstatic volatile uint16_t *hud_map;\n'+font+'\n'+table+'\n'+f0+'\n'+f1+'''
static uint32_t state=0x80923705;
static unsigned next(void){state^=state<<13;state^=state>>17;state^=state<<5;return state;}
static unsigned long checks;
static void check(int x,int y,const char *text){
 uint16_t a[1026],b[1026];for(unsigned i=0;i<1026;i++)a[i]=b[i]=(uint16_t)(i*9+7);
 hud_map=a+1;reference_text(x,y,text);hud_map=b+1;candidate_text(x,y,text);
 assert(!memcmp(a,b,sizeof(a)));assert(a[0]==7&&a[1025]==(uint16_t)(1025*9+7));checks++;
}
int main(void){
 for(unsigned ch=0;ch<256;ch++)for(int x=0;x<=31;x++)for(int y=0;y<32;y++){
  char text[4]={(char)ch,'Z','9',0};check(x,y,text);
 }
 for(unsigned i=0;i<100000;i++){
  char text[91];unsigned length=next()%90;
  for(unsigned n=0;n<length;n++)text[n]=(char)next();text[length]=0;check(next()%32,next()%32,text);
 }
 printf("{\\"result\\":\\"PASS\\",\\"checks\\":%lu,\\"allInputByteValues\\":256,\\"allVisibleAndRightClippedPositions\\":true,\\"tileMapAndCanariesExact\\":true}\\n",checks);
}
'''
(p/'glyph-oracle.c').write_text(s);results=[]
for mode in ['signed','unsigned']:
 subprocess.run(['gcc','-std=c11','-O2','-f'+mode+'-char',str(p/'glyph-oracle.c'),'-o',str(p/'glyph-oracle')],check=True)
 result=json.loads(subprocess.check_output([str(p/'glyph-oracle')],text=True));result['charMode']=mode;results.append(result)
r={'result':'PASS','variants':results,'referenceSourceSha256':hashlib.sha256(base.read_bytes()).hexdigest(),'candidateSourceSha256':hashlib.sha256(candidate.read_bytes()).hexdigest(),'method':'Actual extracted hud_text functions and production font_tile; exact full 32x32 tile-map equality with left/right canaries, all 256 byte values and 100000 randomized strings, each signed/unsigned char mode.'};(p/'glyph-oracle-report.json').write_text(json.dumps(r,indent=2)+'\n');print(r)

(p/'glyph-oracle').unlink(missing_ok=True)
