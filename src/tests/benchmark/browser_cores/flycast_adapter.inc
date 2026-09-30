// Included at the end of pinned rec_wasm.cpp. CPU-only experiment: real SH4
// decode/SSA, WASM block generation and checked production dispatcher. No OS,
// devices, frame scheduler, adaptive hot-block/chain promotion or interrupts.
#include "hw/mem/addrspace.h"
#include "sh4_kernels.h"
static bool bench_ready=false;
static bool bench_compiled[4]={};
static unsigned bench_kind;
static bool bench_init() {
 if(bench_ready)return true;
 settings.platform.system=DC_PLATFORM_DREAMCAST;
 settings.platform.ram_size=16*1024*1024;settings.platform.ram_mask=RAM_SIZE-1;
 settings.platform.vram_size=8*1024*1024;settings.platform.vram_mask=VRAM_SIZE-1;
 settings.platform.aram_size=2*1024*1024;settings.platform.aram_mask=ARAM_SIZE-1;
 addrspace::init();addrspace::initMappings();
 for(unsigned a=0x0c;a<0x100;a+=0x20)addrspace::mapBlock(&mem_b[0],a,a,RAM_MASK);
 IReadMem16=addrspace::read16;ReadMem8=addrspace::read8;
 ReadMem16=addrspace::read16;ReadMem32=addrspace::read32;ReadMem64=addrspace::read64;
 WriteMem8=addrspace::write8;WriteMem16=addrspace::write16;
 WriteMem32=addrspace::write32;WriteMem64=addrspace::write64;
 EM_ASM({Module._flyQ=1;});
 bench_ready=true;return true;
}
extern "C" EMSCRIPTEN_KEEPALIVE int bench_setup(unsigned kind,unsigned count) {
 if(kind>=4||!count)return 0;bench_init();bench_kind=kind;
 const u32 pc=0x0c001000+kind*0x1000;
 if(!bench_compiled[kind]) {
  memcpy(&mem_b[pc&RAM_MASK],sh4_kernels[kind],sh4_kernel_sizes[kind]);
  std::vector<u32> pending={pc};
  for(size_t i=0;i<pending.size();++i){
   u32 cur=pending[i];if(blockByVaddr.count(cur))continue;
   if(cur<pc||cur>=pc+sh4_kernel_sizes[kind])return 0;
   auto *b=new RuntimeBlockInfo();
   if(!b->Setup(cur,Sh4cntx.fpscr))return 0;
   blockByVaddr[cur]=b;
   WasmModuleBuilder builder;if(!buildBlockModule(builder,b))return 0;
   auto &bytes=builder.getBytes();int idx=wasm_compile_block(bytes.data(),bytes.size(),cur);
   if(idx<=0)return 0;primeDispatchEntry(cur,b->sh4_code_size,idx);
   if(b->BranchBlock!=NullAddress)pending.push_back(b->BranchBlock);
   if(b->NextBlock!=NullAddress)pending.push_back(b->NextBlock);
   if(pending.size()>100)return 0;
  }
  bench_compiled[kind]=true;
 }
 memset(&Sh4cntx,0,sizeof(Sh4cntx));
 Sh4cntx.r[0]=0x12345678;Sh4cntx.r[1]=count;
 Sh4cntx.r[2]=0x0c010000;Sh4cntx.r[3]=0x9abcdef0;Sh4cntx.pc=pc;
 unsigned words=kind==3?16384:256;auto *data=(u32*)&mem_b[0x10000];
 for(unsigned i=0;i<words;i++)data[i]=kind==3?((i*109+1021)&16383)*4:i*2654435761u+17;
 return 1;
}
extern "C" EMSCRIPTEN_KEEPALIVE int bench_run(){
 unsigned loops=0;
 while(!Sh4cntx.r[10]){
  Sh4cntx.cycle_counter=10000;g_dispatch_result=0;
  c_dispatch_loop((u32)(uintptr_t)&Sh4cntx,(u32)(uintptr_t)&mem_b[0]);
  if(g_dispatch_result||g_ifb_exception_pending||++loops>1000000)return 0;
 }
 return 1;
}
extern "C" EMSCRIPTEN_KEEPALIVE u32 bench_result(unsigned i){
 if(i<16)return Sh4cntx.r[i];
 if(i==16){u32 h=2166136261u;auto*d=(u32*)&mem_b[0x10000];for(unsigned j=0;j<(bench_kind==3?16384u:256u);j++)h=(h^d[j])*16777619u;return h;}
 return Sh4cntx.pc;
}
