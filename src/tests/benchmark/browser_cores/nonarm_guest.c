#include <stdint.h>
static uint32_t data[16384],result[6];
static void putc_host(char c){__asm__ volatile("outb %0,%1"::"a"(c),"Nd"((unsigned short)0x3f8));}
static void puts_host(const char*s){while(*s)putc_host(*s++);}
static void hex(uint32_t v){for(int i=28;i>=0;i-=4)putc_host("0123456789abcdef"[(v>>i)&15]);putc_host(' ');}
static uint32_t ror(uint32_t v,unsigned n){return (v>>n)|(v<<(32-n));}
__attribute__((noinline)) static void kernel(unsigned kind,unsigned n){
 uint32_t a=0x12345678,b=0x9abcdef0,x=0,y=0;
 while(n){
  if(kind==0){a^=a<<13;a^=a>>17;a^=a<<5;b+=a;x=ror(b,7);a^=x;}
  else if(kind==1){x=n&255;y=data[x];a+=y;y=a^ror(a,11);data[x]=y;b+=y;}
  else if(kind==2){if(a&1)b^=a;else b+=a;x=ror(a,1);a=x^b;a+=a<b?31u:-7u;}
  else {x=data[x>>2];a+=x;b^=ror(a,9);y=data[x>>2];a+=y;x=y;}
  n--;
 }
 result[0]=a;result[1]=n;result[2]=0;result[3]=b;result[4]=x;result[5]=y;
}
void main_guest(void){
 puts_host("CORE_READY\n");
 for(unsigned k=0;k<4;k++)for(unsigned rep=0;rep<8;rep++){
  unsigned words=k==3?16384:256;
  for(unsigned i=0;i<words;i++)data[i]=k==3?((i*109+1021)&16383)*4:i*2654435761u+17;
  puts_host("CORE_START ");hex(k);hex(rep);puts_host("\n");
  kernel(k,1000000);
  puts_host("CORE_END ");hex(k);hex(rep);for(unsigned i=0;i<6;i++)hex(result[i]);
  uint32_t hash=2166136261u;for(unsigned i=0;i<words;i++)hash=(hash^data[i])*16777619u;
  hex(hash);puts_host("\n");
 }
 puts_host("CORE_DONE\n");for(;;)__asm__ volatile("hlt");
}
