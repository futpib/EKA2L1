// Matched native/browser C++ reference and current-emitter control. Offline only.
#include "matched_kernel_reference.h"
#include <cpu/12l1r/exclusive_monitor.h>
#include <cpu/aot/arm_translator.h>
#include <cpu/aot/memory_experiment.h>
#include <cpu/aot/wasm_emitter.h>
#include <common/log.h>
#include <common/code_tracking.h>
#include <chrono>
#include <fstream>
#include <iostream>
#include <vector>
#include <stdexcept>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#define API EMSCRIPTEN_KEEPALIVE
#else
#define API
#endif
using namespace eka2l1::arm;
using namespace matched;
static std::uintptr_t guard_begin=0,guard_end=0;
#include "matched_kernels.inc"
static r12l1::exclusive_monitor monitor(1);
static std::unique_ptr<dyncom_core> cpu;
static ARMul_State *state;
static std::vector<unsigned char> memory(0x20000);
static u32 selected, cycles;
static std::vector<u32> events;
static void event(u32 kind,u32 a){events.push_back(kind);events.push_back(a);events.insert(events.end(),state->Reg.begin(),state->Reg.end());events.push_back(state->Cpsr);}
using Fn=u32(*)(ARMul_State*);
static Fn reference,emitted,prototype;
extern "C" {
API u32 ref_read32(ARMul_State*s,u32 a){return s->ReadMemory32(a);}
API u32 ref_read16(ARMul_State*s,u32 a){return s->ReadMemory16(a);}
API u32 ref_read8(ARMul_State*s,u32 a){return s->ReadMemory8(a);}
API void ref_write32(ARMul_State*s,u32 a,u32 v){s->WriteMemory32(a,v);}
API void ref_write16(ARMul_State*s,u32 a,u32 v){s->WriteMemory16(a,static_cast<std::uint16_t>(v));}
API void ref_write8(ARMul_State*s,u32 a,u32 v){s->WriteMemory8(a,static_cast<std::uint8_t>(v));}
}
#ifdef __EMSCRIPTEN__
EM_JS(int, install, (const unsigned char *bytes,unsigned size,const std::uintptr_t *helpers), {
 const raw=i=>WebAssembly.Table.prototype.get.call(wasmTable,HEAPU32[(helpers>>2)+i]);
 const m=new WebAssembly.Module(HEAPU8.slice(bytes,bytes+size));
 const instance=new WebAssembly.Instance(m,{env:{memory:wasmMemory,tlb_read32:raw(0),tlb_write32:raw(1),tlb_read8:raw(2),tlb_write8:raw(3),tlb_read16:raw(4),tlb_write16:raw(5)}});
 return addFunction(instance.exports.run,'ii');
});
#endif
extern "C" {
API void select_kernel(unsigned which) {
 if(!cpu){cpu=std::make_unique<dyncom_core>(&monitor,12);state=matched_kernel_access::state(*cpu);
#define ACCESS(bits,type) cpu->read_##bits##bit=[](u32 a,type*v){event(bits,a);if(std::uint64_t(a)+sizeof(*v)>memory.size())return false;std::memcpy(v,memory.data()+a,sizeof(*v));return true;};cpu->write_##bits##bit=[](u32 a,type*v){event(bits+100,a);if(std::uint64_t(a)+sizeof(*v)>memory.size())return false;std::memcpy(memory.data()+a,v,sizeof(*v));return true;};
 ACCESS(8,std::uint8_t) ACCESS(16,std::uint16_t) ACCESS(32,std::uint32_t) ACCESS(64,std::uint64_t)
#undef ACCESS
 cpu->exception_handler=[](exception_type,u32){return false;};
 }
 selected=which?1879129820:1879455500;cycles=which?7:57;
 reference=which?reference_1879129820:reference_1879455500;
#ifdef __EMSCRIPTEN__
 const auto *code=which?code_1879129820:code_1879455500;
 auto tr=aot::translate_arm_block(reinterpret_cast<const unsigned char*>(code),cycles*4,selected,nullptr,nullptr,true,true,true,true);tr.func.export_name="run";
 auto bytes=aot::build_wasm_module({tr.func},{{"env","tlb_read32",2,true},{"env","tlb_write32",3,false},{"env","tlb_read8",2,true},{"env","tlb_write8",3,false},{"env","tlb_read16",2,true},{"env","tlb_write16",3,false}});
 const std::uintptr_t helpers[]={reinterpret_cast<std::uintptr_t>(ref_read32),reinterpret_cast<std::uintptr_t>(ref_write32),reinterpret_cast<std::uintptr_t>(ref_read8),reinterpret_cast<std::uintptr_t>(ref_write8),reinterpret_cast<std::uintptr_t>(ref_read16),reinterpret_cast<std::uintptr_t>(ref_write16)};
 emitted=reinterpret_cast<Fn>(install(bytes.data(),bytes.size(),helpers));
 tr=aot::translate_arm_block(reinterpret_cast<const unsigned char*>(code),cycles*4,selected,nullptr,nullptr,true,true,true,true,nullptr,true);tr.func.export_name="run";
 bytes=aot::build_wasm_module({tr.func},{{"env","tlb_read32",2,true},{"env","tlb_write32",3,false},{"env","tlb_read8",2,true},{"env","tlb_write8",3,false},{"env","tlb_read16",2,true},{"env","tlb_write16",3,false}});
 prototype=reinterpret_cast<Fn>(install(bytes.data(),bytes.size(),helpers));
#endif
}
API void set_guard(std::uintptr_t begin,std::uintptr_t end) {
 guard_begin=begin;guard_end=end;
#ifdef __EMSCRIPTEN__
 state->aot_code_begin=begin;state->aot_code_end=end;
#endif
}
API void reset(unsigned seed,unsigned budget) {
 events.clear();std::fill(memory.begin(),memory.end(),0);u32 x=seed;
 for(unsigned a=0x10000;a<0x19000;a+=4){x=x*1664525+1013904223;std::memcpy(memory.data()+a,&x,4);}
 for(unsigned i=0;i<16;++i)state->Reg[i]=0x13000+i*64;
 state->Reg[0]=0x12000;state->Reg[1]=0x10000;state->Reg[2]=0x11000;state->Reg[4]=0x13000;state->Reg[5]=0x14000;state->Reg[13]=0x18000;state->Reg[14]=0x1a000;state->Reg[15]=selected;
 state->Cpsr=state->Mode=16;state->NFlag=state->ZFlag=state->CFlag=state->VFlag=state->TFlag=0;
 matched::reference_budget=budget;state->aot_exit=0;state->NirqSig=1;
 set_guard(reinterpret_cast<std::uintptr_t>(memory.data()+0x1f000),reinterpret_cast<std::uintptr_t>(memory.data()+0x1f100));
 cpu->flush_tlb();for(unsigned a=0x10000;a<=0x19000;a+=4096)cpu->set_tlb_page(a,memory.data()+a,prot_read_write);
#ifdef __EMSCRIPTEN__
 state->aot_tlb=reinterpret_cast<std::uintptr_t>(cpu->mem_cache()->entries);
#endif
}
API unsigned invoke(unsigned variant){return (variant==2?prototype:variant?emitted:reference)(state);}
API unsigned char *data_pointer(){return memory.data();}
API u32 *register_pointer(){return state->Reg.data();}
API u32 cpsr_value(){return (state->Cpsr&0x0fffffdf)|(state->NFlag<<31)|(state->ZFlag<<30)|(state->CFlag<<29)|(state->VFlag<<28)|(state->TFlag<<5);}
// Both targets and both browser variants use this exact loop and indirect ABI.
API __attribute__((noinline)) u32 batch(unsigned variant,unsigned calls){
 Fn fn=variant==2?prototype:variant?emitted:reference;std::array<u32,16> initial;std::memcpy(initial.data(),state->Reg.data(),64);u32 sum=0;
 for(unsigned i=0;i<calls;++i){std::memcpy(state->Reg.data(),initial.data(),64);state->aot_exit=0;sum+=fn(state);}
 return sum;
}
}
#ifdef __EMSCRIPTEN__
extern "C" API unsigned check_edges() {
 unsigned checks=0;
 for(unsigned k=0;k<2;++k){select_kernel(k);
 for(unsigned scenario=0;scenario<8;++scenario)for(unsigned budget=0;budget<=cycles;++budget){
 std::vector<unsigned char> saved_memory;std::vector<u32> saved_state,saved_events;
 for(unsigned variant=0;variant<2;++variant){reset(17,budget);
 if(scenario==1)cpu->flush_tlb();
 if(scenario==2)state->Cpsr|=0x200;
 if(scenario==3){++state->Reg[1];++state->Reg[2];++state->Reg[13];}
 if(scenario==4)for(unsigned a=0x10000;a<=0x19000;a+=4096)cpu->set_tlb_page(a,memory.data()+a,prot_read);
 if(scenario==5){guard_begin=reinterpret_cast<std::uintptr_t>(memory.data()+0x17ff0);guard_end=guard_begin+16;}
 if(scenario==6){cpu->set_tlb_page(0x12000,memory.data()+0x11000,prot_read_write);guard_begin=reinterpret_cast<std::uintptr_t>(memory.data()+0x11000);guard_end=guard_begin+64;}
 if(scenario==7){state->Reg[1]=0;cpu->set_tlb_page(0,memory.data(),prot_read);}
 state->aot_code_begin=guard_begin;state->aot_code_end=guard_end;
 auto count=invoke(variant);std::vector<u32> snapshot(state->Reg.begin(),state->Reg.end());
 snapshot.insert(snapshot.end(),{count,state->Cpsr,state->NFlag,state->ZFlag,state->CFlag,state->VFlag,state->TFlag,state->aot_exit});
 if(!variant){saved_memory=memory;saved_state=snapshot;saved_events=events;}
 else if(saved_memory!=memory || saved_state!=snapshot || saved_events!=events)throw std::runtime_error("edge mismatch kernel="+std::to_string(k)+" scenario="+std::to_string(scenario)+" budget="+std::to_string(budget)+" memory="+std::to_string(saved_memory==memory)+" state="+std::to_string(saved_state==snapshot)+" events="+std::to_string(saved_events==events));
 }++checks;
 }}return checks;
}
#endif
#include "validated_layout_support.inc"

int main(int argc,char **argv){
 // These fixtures publish raw TLB entries rather than a process memory view.
 aot::memory_experiment::mode=0;
 // The reference's overlap scenarios require code-write guards.
 eka2l1::common::code_tracking::unsafe_code_mode=0;
 eka2l1::log::filterings=std::make_unique<eka2l1::log_filterings>();eka2l1::log::filterings->reset_all(spdlog::level::off);
#ifndef __EMSCRIPTEN__
 if(argc!=2)throw std::runtime_error("matched_kernel NATIVE_ORACLE_DIR");
 for(unsigned k=0;k<2;++k){select_kernel(k);unsigned comparisons=0;
 for(unsigned seed=1;seed<=16;++seed)for(unsigned budget=0;budget<=cycles;++budget){reset(seed,budget);auto count=invoke(0);
 std::ifstream in(std::string(argv[1])+"/"+std::to_string(selected)+"-"+std::to_string(seed)+"-"+std::to_string(budget)+".bin",std::ios::binary);
 std::vector<char> expected((std::istreambuf_iterator<char>(in)),{});u32 cpsr=cpsr_value();
 if(expected.size()!=76+memory.size()||count!=budget||std::memcmp(expected.data(),state->Reg.data(),64)||std::memcmp(expected.data()+64,&cpsr,4)||std::memcmp(expected.data()+76,memory.data(),memory.size()))throw std::runtime_error("oracle mismatch "+std::to_string(selected)+" "+std::to_string(seed)+"/"+std::to_string(budget));++comparisons;}
 reset(72,cycles);batch(0,500000);std::cout<<"{\"pc\":"<<selected<<",\"comparisons\":"<<comparisons<<",\"calls\":5000000,\"ms\":[";
 for(unsigned round=0;round<8;++round){reset(72,cycles);auto start=std::chrono::steady_clock::now();if(batch(0,5000000)!=cycles*5000000)throw std::runtime_error("batch count");auto elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();if(round)std::cout<<',';std::cout<<elapsed;}std::cout<<"]}"<<std::endl;
 }
#endif
}
