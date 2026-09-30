// Full EKA CPU entry path: demand compilation, exact validation, and dispatch.
// Flat bounded RAM callbacks; no Symbian scheduler, graphics, audio or devices.
#include <cpu/dyncom/arm_dyncom.h>
#include <cpu/12l1r/exclusive_monitor.h>
#include <cpu/aot/aot_runtime.h>
#include <cpu/aot/code_cache.h>
#include <common/log.h>
#include <common/performance.h>
#include <common/code_tracking_config.h>
#if defined(EKA2L1_WASM_CODE_VERSIONS) || defined(EKA2L1_WASM_CODE_LIFECYCLE) || defined(EKA2L1_WASM_CODE_WRITE_PROTECTION)
#error Full CPU comparison must use the delivered exact-byte validation policy
#endif
#include <emscripten.h>
#include <memory>
#include <cstring>
#include <cstdlib>
#include <atomic>
#include "kernels.h"
using namespace eka2l1::arm;
static constexpr unsigned size=256*1024;
static unsigned char ram[size] __attribute__((aligned(4096)));
static r12l1::exclusive_monitor monitor(1);
static std::unique_ptr<dyncom_core> cpu;
static std::atomic<std::uint64_t> mapping{1};
static unsigned selected=0,words=256;static bool fault=false;
static void init(){
 if(cpu)return;
 eka2l1::log::filterings=std::make_unique<eka2l1::log_filterings>();
 eka2l1::log::filterings->reset_all(spdlog::level::off);
 r12l1::dyncom_folded_tlb=true;aot::code_compare_mode=2;
 setenv("EKA2L1_AOT_RAM","1",1);setenv("EKA2L1_AOT_CHAIN","1",1);
 setenv("EKA2L1_AOT_REGION","1",1);setenv("EKA2L1_AOT_IR_MODE","7",1);
 cpu=std::make_unique<dyncom_core>(&monitor,12);cpu->set_asid(1);
 cpu->code_mapping_generation=&mapping;cpu->code_address_space=1;
 cpu->resolve_code=[](unsigned a,core::code_mapping &view){
  for(unsigned k=0;k<4;k++){unsigned base=0x1000+k*0x1000;
   if(a>=base && a<base+kernel_sizes[k]){view={1,ram+a,base+kernel_sizes[k]-a};return true;}}
  return false;
 };
 cpu->read_code=[](unsigned a,unsigned*v){if(a>size-4)return false;std::memcpy(v,ram+a,4);return true;};
#define ACCESS(bits,type) cpu->read_##bits##bit=[](unsigned a,type*v){if(a>size-sizeof(*v))return false;std::memcpy(v,ram+a,sizeof(*v));return true;};cpu->write_##bits##bit=[](unsigned a,type*v){if(a>size-sizeof(*v))return false;std::memcpy(ram+a,v,sizeof(*v));return true;};
 ACCESS(8,std::uint8_t) ACCESS(16,std::uint16_t) ACCESS(32,std::uint32_t) ACCESS(64,std::uint64_t)
#undef ACCESS
 cpu->exception_handler=[](exception_type,unsigned){fault=true;cpu->stop();return false;};
 cpu->system_call_handler=[](unsigned){fault=true;cpu->stop();};
 for(unsigned k=0;k<4;k++)std::memcpy(ram+0x1000+k*0x1000,kernels[k],kernel_sizes[k]);
 aot::configure_hot_rom(nullptr,0,0,true);
}
extern "C" {
EMSCRIPTEN_KEEPALIVE void setup(unsigned kind,unsigned count){
 init();if(kind>=4||!count)std::abort();selected=kind;words=kind==3?16384:256;fault=false;
 core::thread_context state{};state.cpsr=16;state.cpu_registers[0]=0x12345678;
 state.cpu_registers[1]=count;state.cpu_registers[2]=65536;state.cpu_registers[3]=0x9abcdef0;
 state.cpu_registers[15]=0x1000+kind*0x1000;cpu->load_context(state);
 for(unsigned i=0;i<words;i++)((std::uint32_t*)(ram+65536))[i]=kind==3?((i*109u+1021u)&16383u)*4u:i*2654435761u+17u;
 cpu->flush_tlb();for(unsigned a=0;a<size;a+=4096)cpu->set_tlb_page(a,ram+a,prot_read_write_exec);
}
EMSCRIPTEN_KEEPALIVE unsigned run(unsigned){
 unsigned batches=0;while(!cpu->get_reg(10)&&!fault){cpu->run(10000);if(++batches>1000000)return 0;}
 return !fault;
}
EMSCRIPTEN_KEEPALIVE unsigned result(unsigned i){
 if(i<16)return cpu->get_reg(i);unsigned h=2166136261u;
 for(unsigned j=0;j<words;j++)h=(h^((std::uint32_t*)(ram+65536))[j])*16777619u;return h;
}
EMSCRIPTEN_KEEPALIVE void census(unsigned enable){
 namespace p=eka2l1::common::performance;
 p::enabled=enable;p::detailed=true;p::phase=2;
 if(enable){p::aot_instructions=0;p::aot_dispatches=0;p::decoded_instructions=0;}
}
EMSCRIPTEN_KEEPALIVE double statistic(unsigned i){
 namespace p=eka2l1::common::performance;
 return i==0?p::aot_instructions:i==1?p::aot_dispatches:p::decoded_instructions;
}
// Change code through its host backing without compiled-cache invalidation or mapping
// generation changes. The normal exact validator must reject the old region.
EMSCRIPTEN_KEEPALIVE void mutate(unsigned enabled){
 std::uint32_t instruction=enabled?0xe0200600u:0xe0200680u;
 std::memcpy(ram+0x1000,&instruction,4);
 cpu->clear_instruction_cache(); // Interpreter coherence only; AOT cache remains.
}
EMSCRIPTEN_KEEPALIVE unsigned compiled(){return aot::compiled_function_count();}
}
