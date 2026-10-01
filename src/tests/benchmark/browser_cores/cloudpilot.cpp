#include "common.h"
#include "CPU.h"
#include "RAM.h"
#include "memory_buffer.h"
#include "system_state.h"
#include "patch_dispatch.h"
#include "pace_patch.h"
#include "icache.h"
static ArmCpu *cpu;
static MemoryBuffer memory;
static PacePatch pp{};
extern "C" EMSCRIPTEN_KEEPALIVE void setup(int kind,int n) {
 if(!cpu){
  auto mem=memInit();
  memory.size=RAM_SIZE;memory.buffer=ram;memory.dirtyPagesSize=RAM_SIZE/32768*4;
  memory.dirtyPages=(uint32_t*)calloc(memory.dirtyPagesSize,1);
  auto mapped=ramInit(mem,nullptr,0,RAM_SIZE,&memory,true);
  ramSetFramebuffer(mapped,RAM_SIZE-4096,4096);
  auto pd=initPatchDispatch();
  cpu=cpuInit(4096,mem,ARM_MEMORY_SYSTEM_MMU,false,false,0,0x41069265,0,pd,&pp,createSystemState());
  patchDispatchSetCpu(pd,cpu);
 }
 cpuReset(cpu,4096); memory_init(kind);
 for(int i=0;i<15;i++)cpuSetReg(cpu,i,0);
 cpuSetReg(cpu,0,0x12345678);cpuSetReg(cpu,1,n);cpuSetReg(cpu,2,65536);cpuSetReg(cpu,3,0x9abcdef0);
}
extern "C" EMSCRIPTEN_KEEPALIVE int run(int unused){
 unsigned batches=0;auto regs=cpuGetRegisters(cpu);
 while(!regs[10] && batches++<10000000)(unused ? cpuCycle<ARM_MEMORY_SYSTEM_MPU,false>(cpu,200) : cpuCycle<ARM_MEMORY_SYSTEM_MMU,false>(cpu,200));
 return regs[10]==1;
}
extern "C" EMSCRIPTEN_KEEPALIVE uint32_t result(int i){return i==16?memory_hash():cpuGetRegExternal(cpu,i);}
