// Cross-target probe of the real emulator memory/exception callbacks.
// Native uses Dynarmic Step; WASM uses bounded generated regions via the normal runner.
#include <cpu/dyncom/arm_dyncom.h>
#include <cpu/12l1r/exclusive_monitor.h>
#include <cpu/aot/aot_runtime.h>
#include <cpu/aot/arm_translator.h>
#include <cpu/aot/wasm_emitter.h>
#ifdef EKA_MATCHED_REFERENCE
#include "matched_kernel_reference.h"
#endif
#if !defined(__EMSCRIPTEN__) && !defined(EKA_MATCHED_REFERENCE)
#include <cpu/arm_dynarmic.h>
#endif
#include <common/log.h>
#include <common/performance.h>
#include <iostream>
#include <sstream>
#include <vector>
#include <cstring>
using namespace eka2l1::arm;
std::string regs(core &c) {
    std::ostringstream o; o << '[';
    for (int i=0;i<16;++i) { if(i)o<<','; o<<c.get_reg(i); }
    o<<']'; return o.str();
}
struct Fixture {
    core &cpu;
    std::vector<unsigned char> memory=std::vector<unsigned char>(65536);
    unsigned address, policy, partial=0, calls=0, faults=0;
    bool repaired=false;
    std::vector<std::string> events;
    template<class T> bool access(unsigned a,T *v,bool write) {
        // The data page is inaccessible until the exception handler repairs it.
        ++calls;
        const bool ok=(repaired || (partial && a>=address && a-address<partial*4)) && a+sizeof(T)<=memory.size();
        events.push_back(std::string("\"")+(write?"write":"read")+":"+std::to_string(sizeof(T))+":"+std::to_string(a)+":"+(ok?"ok":"fail")+"\"");
        if(ok){if(write)std::memcpy(memory.data()+a,v,sizeof(T));else std::memcpy(v,memory.data()+a,sizeof(T));}
        return ok;
    }
    void install() {
        cpu.read_code=[&](unsigned a,unsigned *v){if(a+4>memory.size())return false;std::memcpy(v,memory.data()+a,4);return true;};
#define ACCESS(bits,type) cpu.read_##bits##bit=[&](unsigned a,type *v){return access(a,v,false);};cpu.write_##bits##bit=[&](unsigned a,type *v){return access(a,v,true);};
        ACCESS(8,std::uint8_t) ACCESS(16,std::uint16_t) ACCESS(32,std::uint32_t) ACCESS(64,std::uint64_t)
#undef ACCESS
        cpu.exception_handler=[&](exception_type type,unsigned a){
            ++faults;
            events.push_back("{\"exception\":"+std::to_string(type)+",\"address\":"+std::to_string(a)+",\"regs\":"+regs(cpu)+",\"cpsr\":"+std::to_string(cpu.get_cpsr())+"}");
            if(policy==0){repaired=true;return true;}
            if(policy==2)cpu.stop();
            // policy 3 requests a retry but leaves the access unresolved.
            return policy==3;
        };
        cpu.system_call_handler=[](unsigned){};
    }
};
int main(int argc, char **argv){
    const bool interpreter=argc==2 && (std::string(argv[1])=="--interpreter" || std::string(argv[1])=="--region-spans-interpreter" || std::string(argv[1])=="--entry-budget-interpreter");
    eka2l1::common::performance::enabled=true;
    eka2l1::common::performance::phase=2;
    eka2l1::log::filterings=std::make_unique<eka2l1::log_filterings>();
    eka2l1::log::filterings->reset_all(spdlog::level::off);
    const bool read_spans=argc==2 && std::string(argv[1])=="--read-spans";
    const bool wide_snapshots=argc==2 && std::string(argv[1])=="--wide-snapshots";
    const bool region_ir=argc==2 && std::string(argv[1])=="--region-ir";
    const bool region_block_spans=region_ir || (argc==2 && std::string(argv[1])=="--region-block-spans");
    const bool region_spans=region_block_spans || (argc==2 && (std::string(argv[1])=="--region-spans" || std::string(argv[1])=="--region-spans-interpreter"));
    const bool three_instructions=read_spans || wide_snapshots;
    const unsigned instruction_count=region_spans?5:three_instructions?3:2;
    const bool deferred=region_spans || three_instructions || (argc==2 && (std::string(argv[1])=="--deferred" || std::string(argv[1])=="--entry-budget-deferred"));
    const bool entry_budget = region_spans || three_instructions || (argc==2 && (std::string(argv[1])=="--entry-budget" || std::string(argv[1])=="--entry-budget-interpreter" || std::string(argv[1])=="--entry-budget-deferred"));
#ifdef EKA_MATCHED_REFERENCE
    if(entry_budget) { std::cerr << "--entry-budget requires the production runner\n"; return 1; }
#endif
    unsigned cases=0,deferred_cases=0;
    std::vector<unsigned> instructions={0xe5910000,0xe5810000,0xe5d10000,0xe5c10000,0xe1d100b0,0xe1c100b0,0xe8b1000d,0xe8a1000d};
    if(entry_budget || deferred || (argc==2 && std::string(argv[1])=="--extended")) {
        instructions.push_back(0xe891000d);instructions.push_back(0xe881000d);
    }
    if(read_spans || region_spans) instructions={0xe5910000};
    if(region_block_spans) instructions={0xe8b10039,0xe8a10039};
    const std::vector<unsigned> addresses=region_spans
        ? std::vector<unsigned>{0x8000u,0x8ff0u,0x8ff4u}
        : read_spans
        ? std::vector<unsigned>{0x8000u,0x8ff8u,0x8ffcu}
        : std::vector<unsigned>{0x8000u,0x8ffdu,0x8ffcu};
    for(unsigned op:instructions)for(unsigned policy=0;policy<4;++policy)
    for(unsigned address:addresses)for(unsigned endian:{0u,0x200u})for(unsigned permission:{0u,1u})for(unsigned partial=0;partial<(!region_spans && (op&0x0e000000u)==0x08000000u?2u:1u);++partial){
#if defined(__EMSCRIPTEN__) || defined(EKA_MATCHED_REFERENCE)
        r12l1::exclusive_monitor monitor(1); dyncom_core cpu(&monitor,12);
#else
        dynarmic_exclusive_monitor monitor(1); dynarmic_core cpu(&monitor);
#endif
        // Span fixtures allow the first load, then fault the second unless its
        // page is mapped. The 0x8ffc case crosses into an unmapped next page.
        Fixture f{cpu, std::vector<unsigned char>(65536),address,policy,region_spans?3u:read_spans?1u:partial};f.install();
        for(unsigned i=0x8000;i<0xa000;++i)f.memory[i]=(i*37+11)&255;
        // MOVS precedes the access, so exception observers see live flags/registers.
        unsigned program[]={region_ir?0xe3a02007u:0xe3b02007u,wide_snapshots?0xe0c54796u:region_spans?0xe5910000u:op,
            wide_snapshots?op:(read_spans||region_spans)?0xe5913004u:0xeafffffeu,
            0xe5914008u,region_block_spans?op:0xe591500cu};
        std::memcpy(f.memory.data()+0x1000,program,sizeof(program));
        for(unsigned i=0;i<16;++i)cpu.set_reg(i,0x12340000+i);
        cpu.set_reg(0,0x87654321);cpu.set_reg(1,address);cpu.set_pc(0x1000);cpu.set_cpsr(0xa0000010|endian);
        // Read-only TLB: permitted control for loads, denied mapping for stores.
        // Unmapped cases always exercise failure callbacks. Partial cases allow
        // the first transferred word before failing subsequent accesses.
        if(permission)cpu.set_tlb_page(address&~4095u,f.memory.data()+(address&~4095u),
            prot_read);
#if defined(__EMSCRIPTEN__) && !defined(EKA_MATCHED_REFERENCE)
        if(!interpreter) {
        // Every suffix is a real runner entry after a deferred access/Step.
        std::vector<aot::wasm_func_def> functions;
        for(unsigned n=0;n<instruction_count;++n)
            functions.push_back(aot::translate_arm_block(reinterpret_cast<unsigned char*>(program+n),
                (instruction_count-n)*4,0x1000+n*4,nullptr,nullptr,true,false,true,true,nullptr,deferred).func);
        if(region_ir && !functions.front().outlined_callee) {
            std::cerr << "IR fault fixture was not compiled through guarded IR\n"; return 4;
        }
        auto bytes=aot::build_wasm_module(functions,{{"env","tlb_read32",2,true},{"env","tlb_write32",3,false},{"env","tlb_read8",2,true},{"env","tlb_write8",3,false},{"env","tlb_read16",2,true},{"env","tlb_write16",3,false}});
        aot::stage_aot_module(std::move(bytes),"hot-rom");aot::instantiate_staged_modules();aot::chaining_enabled=true;
        }
#endif
        const auto initial_memory=f.memory;
        const auto prior_compiled=eka2l1::common::performance::aot_instructions;
#if !defined(__EMSCRIPTEN__) && !defined(EKA_MATCHED_REFERENCE)
        auto prior_count=cpu.get_num_instruction_executed();
        cpu.step();
        if(!entry_budget) prior_count=cpu.get_num_instruction_executed();
#else
        unsigned prior_count=0; // DynCom resets this counter at each run.
        if(!entry_budget) cpu.step();
#endif
#ifdef EKA_MATCHED_REFERENCE
        auto *s=matched_kernel_access::state(cpu);s->aot_budget=1;s->aot_exit=0;
        s->NFlag=s->Cpsr>>31;s->ZFlag=(s->Cpsr>>30)&1;s->CFlag=(s->Cpsr>>29)&1;s->VFlag=(s->Cpsr>>28)&1;
        matched::Frame frame(s,0,0);
        switch(op) {
#define REF_CASE(opcode) case opcode: frame.instruction<opcode>();break;
        REF_CASE(0xe5910000) REF_CASE(0xe5810000) REF_CASE(0xe5d10000) REF_CASE(0xe5c10000)
        REF_CASE(0xe1d100b0) REF_CASE(0xe1c100b0) REF_CASE(0xe8b1000d) REF_CASE(0xe8a1000d)
        REF_CASE(0xe891000d) REF_CASE(0xe881000d)
#undef REF_CASE
        }
        frame.flush();const auto reference_count=frame.count;
#else
#if defined(__EMSCRIPTEN__)
        if(entry_budget) cpu.run(instruction_count); else cpu.step();
#else
        for(unsigned n=1;n<instruction_count;++n) cpu.step();
#endif
#endif
#if defined(__EMSCRIPTEN__) && !defined(EKA_MATCHED_REFERENCE)
        const auto compiled=eka2l1::common::performance::aot_instructions-prior_compiled;
        if(deferred && compiled<instruction_count)++deferred_cases;
        if(!interpreter && (region_spans ? (compiled>5 || (permission && !endian && address<=0x8ff0 && (!region_block_spans || (op&(1u<<20))) && compiled!=5)) : three_instructions ? (compiled<(wide_snapshots?2u:1u) || compiled>3) : (compiled!=2 && !(deferred && compiled==1)))){
            std::cerr<<"Unexpected generated instruction count at case "<<cases<<'\n';return 2;
        }
#endif
        unsigned hash=2166136261u;for(auto b:f.memory){hash^=b;hash*=16777619u;}
        std::cout<<"FAULT {\"id\":"<<cases++<<",\"opcode\":"<<op<<",\"policy\":"<<policy<<",\"address\":"<<address<<",\"endian\":"<<endian<<",\"tlb_readonly\":"<<permission<<",\"partial\":"<<(region_spans?f.partial:partial)<<",\"regs\":"<<regs(cpu)<<",\"cpsr\":"<<cpu.get_cpsr()<<",\"count\":"<<(
#ifdef EKA_MATCHED_REFERENCE
        reference_count
#else
        cpu.get_num_instruction_executed()-prior_count
#endif
        )<<",\"memory_hash\":"<<hash<<",\"calls\":"<<f.calls<<",\"faults\":"<<f.faults<<",\"events\":[";
        for(unsigned i=0;i<f.events.size();++i){if(i)std::cout<<',';std::cout<<f.events[i];}std::cout<<"],\"memory_changes\":[";
        bool comma=false;
        for(unsigned i=0;i<f.memory.size();++i)if(f.memory[i]!=initial_memory[i]){
            if(comma)std::cout<<',';comma=true;std::cout<<'['<<i<<','<<unsigned(f.memory[i])<<']';
        }
        std::cout<<"]}\n";
    }
#if defined(__EMSCRIPTEN__) && !defined(EKA_MATCHED_REFERENCE)
    if(deferred){std::cerr<<"Deferred cases: "<<deferred_cases<<'\n';if(!deferred_cases)return 3;}
#endif
}
