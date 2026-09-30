#include <stdio.h>
#include <stdarg.h>
#include "common.h"
#include "arm.h"
#include "mem.h"
uintptr_t vraddrl[0x100000], vwaddrl[0x100000];
int mmu,memmode;

const uint32_t *getpccache(uint32_t a){if(a>=RAM_SIZE)abort();return (uint32_t*)ram;}
uint32_t readmemfl(uint32_t a){abort();}
uint32_t readmemfb(uint32_t a){abort();}
void writememfl(uint32_t a,uint32_t v){abort();}
void writememfb(uint32_t a,uint8_t v){abort();}
void rpclog(const char*f,...){abort();}
void fatal(const char*f,...){abort();}
void error(const char*f,...){abort();}
void cp15_write(uint32_t op,uint32_t v){abort();}
uint32_t cp15_read(uint32_t op){abort();}
void fpaopcode(uint32_t op){abort();}
int opSWI(uint32_t op){abort();}
EMSCRIPTEN_KEEPALIVE void setup(int kind,int n){
 static int initialized=0;
 if(!initialized){arm_init();initialized=1;}
 arm_reset(CPUModel_SA110);updatemode(0x13);cpsr=16;
 memory_init(kind);
 for(unsigned i=0;i<0x100000;i++)vraddrl[i]=vwaddrl[i]=1;
 for(unsigned i=0;i<RAM_SIZE/4096;i++)vraddrl[i]=vwaddrl[i]=(uintptr_t)ram;
 arm.reg[0]=0x12345678;arm.reg[1]=n;arm.reg[2]=65536;arm.reg[3]=0x9abcdef0;
 arm.reg[15]=4104;arm.reg[16]=0xd3;arm.event=0;
}
EMSCRIPTEN_KEEPALIVE int run(int unused){
 int batches=0;
 while(!arm.reg[10] && batches++<10000000)arm_exec();
 return arm.reg[10]==1;
}
EMSCRIPTEN_KEEPALIVE uint32_t result(int i){return i==16?memory_hash():arm.reg[i];}
