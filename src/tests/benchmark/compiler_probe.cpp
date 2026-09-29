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
struct Source : Dynarmic::A32::TranslateCallbacks {
 std::vector<unsigned char> bytes; unsigned start, end;
 std::optional<unsigned> MemoryReadCode(unsigned a) override { unsigned v=0; if(a<0x70000000 || a-0x70000000+4>bytes.size())return {};std::memcpy(&v,bytes.data()+a-0x70000000,4);return v; }
 bool PreCodeReadHook(bool, unsigned pc,Dynarmic::A32::IREmitter& ir) override {if(pc>=end) {ir.SetTerm(Dynarmic::IR::Term::ReturnToDispatch{});return false;}return true;}
 void PreCodeTranslationHook(bool,unsigned,Dynarmic::A32::IREmitter&) override {}
 std::uint64_t GetTicksForCode(bool,unsigned,unsigned) override {return 1;}
};
int main(int argc,char**argv) {
 if(argc!=3 && !(argc==4 && std::string(argv[3])=="--defer-memory")) {std::cerr<<"compiler_probe GAME_EXE NEW_DIRECTORY [--defer-memory]\n";return 1;}
 const bool defer_memory=argc==4;
 if(!std::filesystem::create_directory(argv[2]))return 2;
 std::ifstream input(argv[1],std::ios::binary);std::vector<unsigned char> compressed((std::istreambuf_iterator<char>(input)),{});
 if(compressed.size()<0x9c)return 3;
 Source src;src.bytes.resize(0x783d4);
 eka2l1::flate::bit_input stream(compressed.data()+0x9c,(compressed.size()-0x9c)*8);eka2l1::flate::inflater inflater(stream);inflater.init();
 if(inflater.read(src.bytes.data(),src.bytes.size())!=int(src.bytes.size()))return 4;
 for(auto [pc,size]: {std::pair{0x7006370cu,0xf0u},std::pair{0x70013edcu,0x1cu}}) {
  auto prefix=std::string(argv[2])+"/"+std::to_string(pc);src.start=pc;src.end=pc+size;
  std::ofstream(prefix+".arm",std::ios::binary).write((char*)src.bytes.data()+pc-0x70000000,size);
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
  auto t=translate_arm_block(src.bytes.data()+pc-0x70000000,size,pc,nullptr,nullptr,true,true,true,true,nullptr,defer_memory);
  t.func.export_name="run";
  auto wasm=build_wasm_module({t.func},{{"env","tlb_read32",2,true},{"env","tlb_write32",3,false},{"env","tlb_read8",2,true},{"env","tlb_write8",3,false},{"env","tlb_read16",2,true},{"env","tlb_write16",3,false}});
  std::ofstream(prefix+".wasm",std::ios::binary).write((char*)wasm.data(),wasm.size());
 }
}
