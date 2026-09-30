// Offline layout discriminator only. Every dispatch uses the production cache
// validator; clones share the same guest code and reset state before each call.
#include <cpu/aot/code_cache.h>
static aot::validated_code_cache layout_cache;
static std::vector<unsigned char> layout_code, layout_remap;
static std::array<u32,16> layout_initial_regs;
static std::atomic<std::uint64_t> layout_generation{1};
static const unsigned char *layout_backing=nullptr;
static bool layout_mapped=true;
static unsigned layout_resolutions=0;
extern "C" {
API unsigned layout_prepare(ARMul_State *s) {
 if(s!=state)throw std::runtime_error("layout state mismatch");
 std::memcpy(s->Reg.data(),layout_initial_regs.data(),64);
 s->Cpsr=s->Mode=16;s->NFlag=s->ZFlag=s->CFlag=s->VFlag=s->TFlag=0;s->aot_exit=0;
 auto *entry=layout_cache.find(selected,*cpu);
 if(!entry)return 0;
 s->aot_code_begin=entry->guard_begin;s->aot_code_end=entry->guard_end;
 return 1;
}
API void layout_capture() { std::memcpy(layout_initial_regs.data(),state->Reg.data(),64); }
API void layout_reset_cache() {
 const auto *code=selected==1879129820?code_1879129820:code_1879455500;
 layout_code.assign(reinterpret_cast<const unsigned char*>(code),reinterpret_cast<const unsigned char*>(code)+cycles*4);
 layout_remap=layout_code;layout_backing=layout_code.data();layout_mapped=true;layout_resolutions=0;
 layout_cache={};++layout_generation;cpu->code_address_space=1;cpu->code_mapping_generation=&layout_generation;
 cpu->resolve_code=[](u32 pc,core::code_mapping &view){
  ++layout_resolutions;
  if(!layout_mapped || pc!=selected)return false;
  view={cpu->code_address_space,layout_backing,layout_code.size()};return true;
 };
 auto &entry=layout_cache.insert(selected,{1,layout_backing,layout_code.size()},layout_code.size());
 layout_cache.attach(entry.version,emitted);
}
API void layout_mutate(unsigned mode) {
 if(mode==1)layout_code.back()^=1;
 else if(mode==2){layout_backing=layout_remap.data();++layout_generation;}
 else if(mode==3){layout_mapped=false;++layout_generation;}
 else if(mode==4)layout_cache.invalidate(selected,4);
 else if(mode==5)cpu->code_address_space=2;
 else if(mode)throw std::runtime_error("unknown layout mutation");
}
API unsigned layout_mapping_calls(){return layout_resolutions;}
API std::uintptr_t layout_state(){return reinterpret_cast<std::uintptr_t>(state);}
API unsigned layout_compare(std::uintptr_t a, std::uintptr_t b, unsigned size) {
 return aot::equal_code_bytes(reinterpret_cast<const unsigned char *>(a),
     reinterpret_cast<const unsigned char *>(b), size);
}
API void layout_compare_mode(unsigned mode) {
 if(mode>3)throw std::runtime_error("unknown exact comparison mode");
 aot::code_compare_mode=mode;
}
}
#ifdef __EMSCRIPTEN__
EM_JS(void, layout_expose_js, (std::uintptr_t validator, std::uintptr_t compare), {
 Module['layoutMemory']=wasmMemory;
 Module['layoutValidate']=WebAssembly.Table.prototype.get.call(wasmTable,validator);
 Module['layoutCompare']=WebAssembly.Table.prototype.get.call(wasmTable,compare);
});
extern "C" API void layout_expose(){layout_expose_js(reinterpret_cast<std::uintptr_t>(layout_prepare),
 reinterpret_cast<std::uintptr_t>(layout_compare));}
#endif
