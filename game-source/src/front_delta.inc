/* Lossless sequential frontend frames. Caller supplies the previous 240x160
 * image and scratch from the suspended scene arena; no persistent RAM. */
static unsigned front_delta_word(const uint8_t *p){
 return p[0]|((unsigned)p[1]<<8)|((unsigned)p[2]<<16)|((unsigned)p[3]<<24);
}
static UI_FN unsigned front_delta_decode(const uint8_t *source,unsigned frame,
                                        uint8_t *pixels,uint8_t *scratch,unsigned capacity){
 if(frame>=front_delta_word(source))return 0;
 if(!frame)for(unsigned i=0;i<9600;i++)((uint32_t*)pixels)[i]=0;
 const uint8_t *record=source+front_delta_word(source+4+frame*4);
 unsigned kind=*record++;
 if(!kind)return 1;
 if(kind==1)return lz_decode(record,pixels,38400)==38400;
 if(kind>3)return 0;
 unsigned size=lz_decode(record,scratch,capacity);
 if(kind==2){
  if(size!=38400)return 0;
  for(unsigned i=0;i<9600;i++)((uint32_t*)pixels)[i]^=((uint32_t*)scratch)[i];
  return 1;
 }
 for(unsigned at=0;at<size;){
  if(size-at<4)return 0;
  unsigned offset=scratch[at]|((unsigned)scratch[at+1]<<8);
  unsigned count=scratch[at+2]|((unsigned)scratch[at+3]<<8);at+=4;
  if(!count||((offset|count|at)&3)||offset+count>38400||count>size-at)return 0;
  dma(scratch+at,pixels+offset,count);
  at+=count;
 }
 return size!=0;
}
