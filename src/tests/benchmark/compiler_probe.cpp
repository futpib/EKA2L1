// Standalone research tool. Does not alter the emulator or installed game.
#include <common/flate.h>
#include <cpu/aot/arm_translator.h>
#include <cpu/aot/wasm_emitter.h>
#include <dynarmic/frontend/A32/a32_location_descriptor.h>
#include <dynarmic/frontend/A32/a32_ir_emitter.h>
#include <dynarmic/frontend/A32/translate/a32_translate.h>
#include <dynarmic/frontend/A32/translate/translate_callbacks.h>
#include <dynarmic/ir/basic_block.h>
#include <dynarmic/ir/opt/passes.h>
#include <fstream>
#include <iostream>
#include <vector>
#include <cstring>
#include <filesystem>
#include <chrono>
#include <algorithm>
#include <capstone/capstone.h>
#include <iomanip>
struct Source : Dynarmic::A32::TranslateCallbacks {
 std::vector<unsigned char> bytes; unsigned start, end;
 std::optional<unsigned> MemoryReadCode(unsigned a) override { unsigned v=0; if(a<0x70000000 || a-0x70000000+4>bytes.size())return {};std::memcpy(&v,bytes.data()+a-0x70000000,4);return v; }
 bool PreCodeReadHook(bool, unsigned pc,Dynarmic::A32::IREmitter& ir) override {if(pc>=end) {ir.SetTerm(Dynarmic::IR::Term::ReturnToDispatch{});return false;}return true;}
 void PreCodeTranslationHook(bool,unsigned,Dynarmic::A32::IREmitter&) override {}
 std::uint64_t GetTicksForCode(bool,unsigned,unsigned) override {return 1;}
};
int main(int argc,char**argv) {
 bool defer_memory=false, inline_leaves=false;
 auto ir_policy=eka2l1::arm::aot::arm_ir_policy::configured;
 std::vector<std::pair<unsigned,unsigned>> regions;
 try {
  for(int i=3;i<argc;++i) {
   const std::string arg=argv[i];
   if(arg=="--defer-memory") defer_memory=true;
   else if(arg=="--inline-leaves") inline_leaves=true;
   else if(arg=="--ir-mode" && i+1<argc) {
    const std::string mode=argv[++i];
    if(mode!="0" && mode!="1" && mode!="2" && mode!="3" && mode!="4" && mode!="5" && mode!="6" && mode!="7")throw std::invalid_argument("IR mode must be 0, 1, 2, 3, 4, 5, 6 or 7");
    ir_policy=static_cast<eka2l1::arm::aot::arm_ir_policy>(mode[0]-'0');
   }
   else if(arg=="--region" && i+2<argc) {
    const auto pc=std::stoul(argv[++i],nullptr,0), size=std::stoul(argv[++i],nullptr,0);
    if(pc>0xffffffffu || size>0xffffffffu) throw std::invalid_argument("region overflow");
    regions.emplace_back(pc,size);
   } else throw std::invalid_argument("unknown option");
  }
  if(argc<3) throw std::invalid_argument("missing arguments");
 } catch(const std::exception &e) {
  std::cerr<<"compiler_probe GAME_EXE NEW_DIRECTORY [--defer-memory] [--inline-leaves] [--ir-mode 0/1/2/3/4/5/6/7] [--region PC SIZE]...\n"<<e.what()<<"\n";return 1;
 }
 if(regions.empty()) regions={{0x7006370cu,0xf0u},{0x70013edcu,0x1cu}};
 if(!std::filesystem::create_directory(argv[2]))return 2;
 std::ifstream input(argv[1],std::ios::binary);std::vector<unsigned char> compressed((std::istreambuf_iterator<char>(input)),{});
 if(compressed.size()<0x9c)return 3;
 Source src;src.bytes.resize(0x783d4);
 eka2l1::flate::bit_input stream(compressed.data()+0x9c,(compressed.size()-0x9c)*8);eka2l1::flate::inflater inflater(stream);inflater.init();
 if(inflater.read(src.bytes.data(),src.bytes.size())!=int(src.bytes.size()))return 4;
 for(auto [pc,size]: regions) {
  if(pc<0x70000000u || (pc&3) || !size || (size&3) || std::uint64_t(pc-0x70000000u)+size>src.bytes.size()) return 5;
  auto prefix=std::string(argv[2])+"/"+std::to_string(pc);src.start=pc;src.end=pc+size;
  std::ofstream(prefix+".arm",std::ios::binary).write((char*)src.bytes.data()+pc-0x70000000,size);
  auto disassemble = [&](unsigned address, const std::uint8_t *bytes, std::size_t length, const std::string &path) {
   csh handle; if(cs_open(CS_ARCH_ARM,CS_MODE_ARM,&handle)!=CS_ERR_OK) return false;
   cs_insn *insns=nullptr; const auto count=cs_disasm(handle,bytes,length,address,0,&insns);
   std::ofstream out(path);
   for(std::size_t i=0;i<count;++i) out<<std::hex<<insns[i].address<<"  "<<insns[i].mnemonic<<" "<<insns[i].op_str<<"\n";
   cs_free(insns,count);cs_close(&handle);return count*4==length;
  };
  if(!disassemble(pc,src.bytes.data()+pc-0x70000000,size,prefix+".asm")) return 6;
  auto translate = [&] { return Dynarmic::A32::Translate(Dynarmic::A32::LocationDescriptor(pc,Dynarmic::A32::PSR(0x10),Dynarmic::A32::FPSCR(0)),&src,{Dynarmic::A32::ArchVersion::v6K}); };
  auto optimize = [](auto &block) {
   Dynarmic::Optimization::A32GetSetElimination(block,{});
   Dynarmic::Optimization::DeadCodeElimination(block);
   Dynarmic::Optimization::ConstantPropagation(block);
   Dynarmic::Optimization::DeadCodeElimination(block);
   Dynarmic::Optimization::IdentityRemovalPass(block);
  };
  auto block=Dynarmic::A32::Translate(Dynarmic::A32::LocationDescriptor(pc,Dynarmic::A32::PSR(0x10),Dynarmic::A32::FPSCR(0)),&src,{Dynarmic::A32::ArchVersion::v6K});
  std::ofstream(prefix+".before.ir")<<Dynarmic::IR::DumpBlock(block);
  optimize(block);
  std::ofstream(prefix+".after.ir")<<Dynarmic::IR::DumpBlock(block);
  std::vector<double> samples;
  for (int i=0;i<101;++i) {
   auto start=std::chrono::steady_clock::now();
   auto measured=translate(); optimize(measured);
   auto us=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-start).count();
   if(i) samples.push_back(us);
  }
  std::sort(samples.begin(),samples.end());
  std::cout<<pc<<" native_translate_optimize_us_median "<<samples[50]<<" min "<<samples.front()<<" max "<<samples.back()<<" samples 100 (excludes IO and WASM emission)\n";
  using namespace eka2l1::arm::aot;
  leaf_resolver resolve=[&](std::uint32_t address) {
   if(address<0x70000000u || std::uint64_t(address-0x70000000u)+64>src.bytes.size()) return std::vector<std::uint8_t>{};
   const auto begin=src.bytes.begin()+address-0x70000000u;
   return std::vector<std::uint8_t>(begin,begin+64);
  };
  auto t=translate_arm_block(src.bytes.data()+pc-0x70000000,size,pc,nullptr,nullptr,true,true,true,true,inline_leaves?&resolve:nullptr,defer_memory,ir_policy);
  std::cout<<pc<<" region_end "<<t.end_address<<" body_bytes "<<t.func.body.size()<<" dependencies "<<t.dependencies.size()<<" guarded_ir "<<bool(t.func.outlined_callee)<<" ir_segments "<<t.ir_segments<<" ir_outlined_segments "<<t.ir_outlined_segments<<" proved_reads "<<t.proved_reads<<" proved_writes "<<t.proved_writes<<" budget_chunks "<<t.budget_chunks<<" ir_cold_values "<<t.ir_cold_values<<" ir_cold_halves "<<t.ir_cold_halves<<" ir_policy "<<static_cast<int>(ir_policy)<<"\n";
  for(const auto &dependency:t.dependencies) {
   const auto name=prefix+"-leaf-"+std::to_string(dependency.address);
   std::ofstream(name+".arm",std::ios::binary).write((const char*)dependency.bytes.data(),dependency.bytes.size());
   if(!disassemble(dependency.address,dependency.bytes.data(),dependency.bytes.size(),name+".asm"))return 6;
  }
  t.func.export_name="run";
  auto wasm=build_wasm_module({t.func},{{"env","tlb_read32",2,true},{"env","tlb_write32",3,false},{"env","tlb_read8",2,true},{"env","tlb_write8",3,false},{"env","tlb_read16",2,true},{"env","tlb_write16",3,false}});
  std::ofstream(prefix+".wasm",std::ios::binary).write((char*)wasm.data(),wasm.size());
 }
}
