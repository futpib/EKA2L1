#include <cpu/aot/arm_translator.h>
#include <cpu/aot/wasm_emitter.h>
#include <fstream>
#include "kernels.h"
int main(int argc,char**argv){
 if(argc!=2)return 1;
 using namespace eka2l1::arm::aot;
 for(unsigned i=0;i<3;i++){
  auto tr=translate_arm_block(kernels[i],kernel_sizes[i],4096,nullptr,nullptr,true,true,true,true,nullptr,true,arm_ir_policy::write_budget_chunks);
  tr.func.export_name="run";
  auto m=build_wasm_module({tr.func},{{"env","tlb_read32",2,true},{"env","tlb_write32",3,false},{"env","tlb_read8",2,true},{"env","tlb_write8",3,false},{"env","tlb_read16",2,true},{"env","tlb_write16",3,false}});
  std::ofstream(std::string(argv[1])+"/eka"+std::to_string(i)+".wasm",std::ios::binary).write((char*)m.data(),m.size());
 }
}
