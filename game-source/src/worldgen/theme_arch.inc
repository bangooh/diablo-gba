/* Modified GBA adaptation of Devilution gendung.cpp theme architecture.
 * Original: source_assets/worldgen/gendung.cpp (hash in reference provenance),
 * Sustainable Use License retained in LICENSES/Devilution-LICENSE.md (repository root).
 * Keep native ordering/random calls. Bound the documented native edge reads
 * and the 50-room array; descriptors occupy generator scratch, not live RAM. */
unsigned game_world_theme_count(void){return (unsigned)themeCount;}
int game_world_theme_get(unsigned index,WorldThemeRoom *out){
 if(!out||index>=(unsigned)themeCount||index>=WORLD_THEME_LIMIT)return 0;
 *out=ws->theme_rooms[index];return 1;
}
int SkipThemeRoom(int x,int y){
 for(int i=0;i<themeCount;i++){
  const WorldThemeRoom *t=&ws->theme_rooms[i];
  if(x>=(int)t->x-2&&x<=t->x+t->width+2&&y>=(int)t->y-2&&y<=t->y+t->height+2)return 0;
 }
 return 1;
}
static int theme_arch_floor(int x,int y,int floor){
 return (unsigned)x<40&&(unsigned)y<40&&dungeon[x][y]==floor;
}
static int theme_arch_fit(int floor,int x,int y,int minSize,int maxSize,int *width,int *height){
 int xa[20]={0},ya[20]={0},xf=1,yf=1,xc=0,yc=0;
 if(minSize<3||minSize>maxSize||maxSize>20||!SkipThemeRoom(x,y))return 0;
 if(x>40-maxSize&&y>40-maxSize)return 0;
 for(int i=0;i<maxSize;i++){
  if(xf){for(int xx=x;xx<x+maxSize;xx++){
   if(!theme_arch_floor(xx,y+i,floor)){if(xx>=minSize)break;xf=0;}else xc++;
  }if(xf){xa[i]=xc;xc=0;}}
  if(yf){for(int yy=y;yy<y+maxSize;yy++){
   if(!theme_arch_floor(x+i,yy,floor)){if(yy>=minSize)break;yf=0;}else yc++;
  }if(yf){ya[i]=yc;yc=0;}}
 }
 for(int i=0;i<minSize;i++)if(xa[i]<minSize||ya[i]<minSize)return 0;
 int w=xa[0],h=ya[0];
 for(int i=0;i<maxSize&&xa[i]>=minSize&&ya[i]>=minSize;i++){if(xa[i]<w)w=xa[i];if(ya[i]<h)h=ya[i];}
 *width=w-2;*height=h-2;return x+*width<40&&y+*height<40;
}
static void theme_arch_create(unsigned index){
 const WorldThemeRoom *t=&ws->theme_rooms[index];
 int x=t->x,y=t->y,w=t->width,h=t->height;
 unsigned top=leveltype==DTYPE_CAVES?134:2,left=leveltype==DTYPE_CAVES?137:1;
 unsigned floor=leveltype==DTYPE_CATACOMBS?3:leveltype==DTYPE_CAVES?7:6;
 for(int yy=y;yy<y+h;yy++)for(int xx=x;xx<x+w;xx++)
  dungeon[xx][yy]=(uint8_t)(yy==y||yy==y+h-1?top:xx==x||xx==x+w-1?left:floor);
 static const uint8_t corners[3][4]={{8,7,9,6},{150,151,152,138},{9,16,15,12}};
 const uint8_t *c=corners[leveltype-2];
 dungeon[x][y]=c[0];dungeon[x+w-1][y]=c[1];dungeon[x][y+h-1]=c[2];dungeon[x+w-1][y+h-1]=c[3];
 if(!random_(0,2)){
  if(leveltype==DTYPE_HELL){dungeon[x+w-1][y+h/2-1]=53;dungeon[x+w-1][y+h/2]=6;dungeon[x+w-1][y+h/2+1]=52;dungeon[x+w-2][y+h/2-1]=54;}
  else dungeon[x+w-1][y+h/2]=(uint8_t)(leveltype==DTYPE_CAVES?147:4);
 }else{
  if(leveltype==DTYPE_HELL){dungeon[x+w/2-1][y+h-1]=57;dungeon[x+w/2][y+h-1]=6;dungeon[x+w/2+1][y+h-1]=56;dungeon[x+w/2][y+h-2]=59;dungeon[x+w/2-1][y+h-2]=58;}
  else dungeon[x+w/2][y+h-1]=(uint8_t)(leveltype==DTYPE_CAVES?146:5);
 }
}
void DRLG_PlaceThemeRooms(int minSize,int maxSize,int floor,int freq,int rndSize){
 themeCount=0;memset(ws->theme_rooms,0,sizeof ws->theme_rooms);
 for(int j=0;j<40;j++)for(int i=0;i<40;i++){
  int w,h;if(themeCount>=WORLD_THEME_LIMIT)return;
  if(dungeon[i][j]!=floor||random_(0,freq)||!theme_arch_fit(floor,i,j,minSize,maxSize,&w,&h))continue;
  if(rndSize){int min=minSize-2,max=maxSize-2,n=min+random_(0,random_(0,w-min+1));w=n>=min&&n<=max?n:min;n=min+random_(0,random_(0,h-min+1));h=n>=min&&n<=max?n:min;}
  WorldThemeRoom *t=&ws->theme_rooms[themeCount];*t=(WorldThemeRoom){i+1,j+1,w,h,0};
  if(leveltype==DTYPE_CAVES)game_rooms_rect(2*i+20,2*j+20,2*(i+w)+15,2*(j+h)+15,-1);
  else game_rooms_macro_rect(i+1,j+1,i+w,j+h,-1);
  uint8_t *rooms=game_rooms_candidate();if(!rooms)return;
  t->room=rooms[(2*i+20)*112+2*j+20];
  theme_arch_create((unsigned)themeCount);themeCount++;
 }
}
