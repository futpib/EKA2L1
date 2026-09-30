#include <stdint.h>
volatile uint32_t results[6];
uint32_t data[16384] __attribute__((aligned(4096)));
extern void kernel0(uint32_t,uint32_t*),kernel1(uint32_t,uint32_t*),kernel2(uint32_t,uint32_t*),kernel3(uint32_t,uint32_t*);
static void puts_host(const char*s){register uint32_t r0 __asm__("r0")=4;register const char*r1 __asm__("r1")=s;__asm__ volatile("svc 0x123456":"+r"(r0):"r"(r1):"memory");}
static void hex(uint32_t n){char s[10];for(int i=0;i<8;i++)s[i]="0123456789abcdef"[(n>>(28-i*4))&15];s[8]=' ';s[9]=0;puts_host(s);}
void main_guest(void){
 puts_host("CORE_READY\n");
 for(unsigned kind=0;kind<4;kind++)for(unsigned rep=0;rep<8;rep++){
  unsigned words=kind==3?16384:256;
  for(unsigned i=0;i<words;i++)data[i]=kind==3?((i*109u+1021u)&16383u)*4u:i*2654435761u+17;
  puts_host("CORE_START ");hex(kind);hex(rep);puts_host("\n");
  if(kind==0)kernel0(1000000,data);else if(kind==1)kernel1(1000000,data);else if(kind==2)kernel2(1000000,data);else kernel3(1000000,data);
  puts_host("CORE_END ");hex(kind);hex(rep);
  for(unsigned i=0;i<6;i++)hex(results[i]);
  uint32_t h=2166136261u;for(unsigned i=0;i<words;i++)h=(h^data[i])*16777619u;hex(h);puts_host("\n");
 }
 puts_host("CORE_DONE\n");
 for(;;)__asm__ volatile("wfi");
}
