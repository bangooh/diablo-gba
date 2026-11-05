/* Host asset packer. Exact byte-cost parsing of standard LZ10, including the
 * flag byte every eight tokens. The console decoder and pixels are unchanged. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
size_t lz10_optimal(const uint8_t *in, size_t n, uint8_t *out) {
 int *prev=malloc(n*sizeof(int)), *heads=malloc(65536*sizeof(int));
 uint8_t *length=calloc(n,1), *choice=calloc(n*8,1);
 uint16_t *distance=calloc(n,sizeof(uint16_t));
 uint32_t *cost=calloc((n+1)*8,sizeof(uint32_t));
 if(!prev||!heads||!length||!choice||!distance||!cost)abort();
 memset(heads,255,65536*sizeof(int));
 for(size_t p=0;p<n;p++){
  if(p+3>n)continue;
  unsigned key=((in[p]*251u+in[p+1])*251u+in[p+2])&65535;
  unsigned best=0,dist=0,limit=n-p<18?n-p:18;
  for(int q=heads[key];q>=0&&p-(size_t)q<=4096;q=prev[q]){
   unsigned k=0;
   while(k<limit&&in[q+k]==in[p+k])k++;
   if(k>best){best=k;dist=p-q;if(k==limit)break;}
  }
  length[p]=best;distance[p]=dist;prev[p]=heads[key];heads[key]=p;
 }
 for(size_t p=n;p-->0;)for(unsigned bit=0;bit<8;bit++){
  unsigned next=(bit+1)&7,flag=bit==0,chosen=1;
  uint32_t best=flag+1+cost[(p+1)*8+next];
  for(unsigned k=3;k<=length[p];k++){
   uint32_t c=flag+2+cost[(p+k)*8+next];
   if(c<=best){best=c;chosen=k;}
  }
  cost[p*8+bit]=best;choice[p*8+bit]=chosen;
 }
 out[0]=16;out[1]=n;out[2]=n>>8;out[3]=n>>16;
 size_t pos=4,p=0;
 while(p<n){size_t flags=pos++;out[flags]=0;
  for(unsigned bit=0;bit<8&&p<n;bit++){
   unsigned k=choice[p*8+bit];
   if(k>=3){unsigned token=((k-3)<<12)|(distance[p]-1);
    out[flags]|=128>>bit;out[pos++]=token>>8;out[pos++]=token;
   }else out[pos++]=in[p];
   p+=k;
  }
 }
 free(prev);free(heads);free(length);free(choice);free(distance);free(cost);
 return pos;
}
