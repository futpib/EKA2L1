#include <stdbool.h>
#define FORCE_INLINE inline __attribute__((always_inline))
#define SB_LIKELY(x) __builtin_expect(!!(x),1)
#define SB_UNLIKELY(x) __builtin_expect(!!(x),0)
#include "common.h"
#define SB_BFE(v,b,n) (((v)>>(b))&((1u<<(n))-1))
#include "arm7.h"
static arm7_t cpu;
static uint32_t r32(void*u,uint32_t a){if(a>RAM_SIZE-4)abort();uint32_t v;memcpy(&v,ram+a,4);return v;}
static uint32_t r16(void*u,uint32_t a){if(a>RAM_SIZE-2)abort();uint16_t v;memcpy(&v,ram+a,2);return v;}
static uint8_t r8(void*u,uint32_t a){if(a>=RAM_SIZE)abort();return ram[a];}
static uint32_t rs32(void*u,uint32_t a,bool s){return r32(u,a);}
static uint32_t rs16(void*u,uint32_t a,bool s){return r16(u,a);}
static void w32(void*u,uint32_t a,uint32_t v){if(a>RAM_SIZE-4)abort();memcpy(ram+a,&v,4);}
static void w16(void*u,uint32_t a,uint16_t v){if(a>RAM_SIZE-2)abort();memcpy(ram+a,&v,2);}
static void w8(void*u,uint32_t a,uint8_t v){if(a>=RAM_SIZE)abort();ram[a]=v;}
EMSCRIPTEN_KEEPALIVE void setup(int kind,int n) {
 static bool initialized=false;
 static arm7_t initial;
 if(!initialized){initial=arm7_init(NULL);initialized=true;}
 cpu=initial; memory_init(kind);
 cpu.read32=r32;cpu.read16=r16;cpu.read8=r8;cpu.read32_seq=rs32;cpu.read16_seq=rs16;
 cpu.write32=w32;cpu.write16=w16;cpu.write8=w8;
 cpu.registers[0]=0x12345678;cpu.registers[1]=n;cpu.registers[2]=65536;cpu.registers[3]=0x9abcdef0;
 cpu.registers[15]=4096;cpu.registers[16]=0xd3;
}
EMSCRIPTEN_KEEPALIVE int run(int arm9) {
 int batches=0;
 while(!cpu.registers[10] && batches++<10000000) {
  if(arm9)for(int i=0;i<200;i++)arm9_exec_instruction(&cpu);
  else for(int i=0;i<200;i++)arm7_exec_instruction(&cpu);
 }
 return cpu.registers[10]==1;
}
EMSCRIPTEN_KEEPALIVE uint32_t result(int i){return i==16?memory_hash():cpu.registers[i];}
