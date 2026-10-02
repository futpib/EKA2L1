// Cross-target probe of the real emulator memory/exception callbacks.
// Native uses Dynarmic Step; WASM uses bounded generated regions via the normal runner.
#include <cpu/dyncom/arm_dyncom.h>
#include <cpu/12l1r/exclusive_monitor.h>
#include <cpu/aot/aot_runtime.h>
#include <cpu/aot/rom_dispatch.h>
#include <cpu/aot/code_cache.h>
#include <cpu/aot/arm_translator.h>
#include <cpu/aot/execution_limits.h>
#include <cpu/aot/exit_census.h>
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
#include <cstdlib>
using namespace eka2l1::arm;
#ifndef EKA_MATCHED_REFERENCE
namespace eka2l1::arm { struct matched_kernel_access { static ARMul_State *state(dyncom_core &c) { return c.state_.get(); } }; }
#endif
std::string regs(core &c) {
    std::ostringstream o; o << '[';
    for (int i=0;i<16;++i) { if(i)o<<','; o<<c.get_reg(i); }
    o<<']'; return o.str();
}
struct Fixture {
    core &cpu;
    std::vector<unsigned char> memory=std::vector<unsigned char>(65536);
    unsigned address, policy, partial=0, calls=0, faults=0;
    bool repaired=false, stopped=false;
    unsigned remap_root=0; bool remapped=false, remap_writable=false;
    std::vector<std::string> events;
    template<class T> bool access(unsigned a,T *v,bool write) {
        // The data page is inaccessible until the exception handler repairs it.
        ++calls;
        const bool ok=(repaired || (partial && a>=address && a-address<partial*4)) && a+sizeof(T)<=memory.size();
        events.push_back(std::string("\"")+(write?"write":"read")+":"+std::to_string(sizeof(T))+":"+std::to_string(a)+":"+(ok?"ok":"fail")+"\"");
        if(ok){
            auto *backing=memory.data()+a;
            if(remapped && (a&~4095u)==remap_root) backing=memory.data()+0xa000+(a&4095u);
            if(write)std::memcpy(backing,v,sizeof(T));else std::memcpy(v,backing,sizeof(T));
        }
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
            if(remap_root) {
                cpu.set_tlb_page(remap_root,memory.data()+0xa000,remap_writable?prot_read_write:prot_read);
                remapped=true;
            }
            if(policy==0){repaired=true;return true;}
            if(policy==2){stopped=true;cpu.stop();}
            // policy 3 requests a retry but leaves the access unresolved.
            return policy==3;
        };
        cpu.system_call_handler=[](unsigned){};
    }
};

// Thumb memory operations through production callbacks and the real runner.
// The memory instruction is last, so stop/fault cases compare its exact
// architectural effects without assuming later instructions execute.
static int thumb_memory_fault_probe(bool direct) {
    aot::thumb_direct_memory=direct;
    std::cout<<"PROBE_THUMB_MEMORY "<<direct<<"\n";
    unsigned cases=0;
    for(unsigned op:{0x6808u,0x6008u,0x7808u,0x7008u,0x8808u,0x8008u,0xc905u,0xc105u,0xb405u,0xbc05u})
    for(unsigned policy=0;policy<4;++policy)for(unsigned address:{0x8000u,0x8ffdu,0x8ffcu})
    for(unsigned endian:{0u,0x200u})for(unsigned permission:{0u,1u,3u}) {
#if defined(__EMSCRIPTEN__) || defined(EKA_MATCHED_REFERENCE)
        r12l1::exclusive_monitor monitor(1);dyncom_core cpu(&monitor,12);
#else
        dynarmic_exclusive_monitor monitor(1);dynarmic_core cpu(&monitor);
#endif
        Fixture f{cpu,std::vector<unsigned char>(65536),address,policy};f.install();
        for(unsigned i=0x7000;i<0xa000;++i)f.memory[i]=(i*37+11)&255;
        const std::uint16_t program[]={0x2207,static_cast<std::uint16_t>(op)};
        std::memcpy(f.memory.data()+0x1000,program,sizeof(program));
        for(unsigned i=0;i<16;++i)cpu.set_reg(i,0x12340000+i);
        cpu.set_reg(0,0x87654321);cpu.set_reg(1,address);cpu.set_reg(13,address);
        cpu.set_pc(0x1000);cpu.set_cpsr(0xA0000030|endian);
        if(permission)cpu.set_tlb_page(address&~4095u,f.memory.data()+(address&~4095u),permission==3?prot_read_write:prot_read);
#ifdef __EMSCRIPTEN__
        aot::global_registry().clear();
        auto translated=aot::translate_thumb_block(reinterpret_cast<const unsigned char *>(program),sizeof(program),0x1000,nullptr,nullptr,true,false,true);
        if(!translated.entry_supported || !translated.complete){std::cerr<<"Thumb fixture did not compile\n";return 2;}
        translated.func.export_name="f_4097";
        auto bytes=aot::build_wasm_module({translated.func},{{"env","tlb_read32",2,true},{"env","tlb_write32",3,false},
            {"env","tlb_read8",2,true},{"env","tlb_write8",3,false},{"env","tlb_read16",2,true},{"env","tlb_write16",3,false}});
        aot::stage_aot_module(std::move(bytes),"thumb-memory-fault");aot::instantiate_staged_modules();aot::chaining_enabled=true;
#endif
        const auto before=f.memory;
#ifdef __EMSCRIPTEN__
        const unsigned prior=0;
        const auto compiled_before=eka2l1::common::performance::aot_instructions;
        cpu.run(2);
        if(eka2l1::common::performance::aot_instructions-compiled_before!=2){std::cerr<<"Thumb fixture not executed by compiled runner\n";return 3;}
#else
        const auto prior=cpu.get_num_instruction_executed();
        cpu.step();if(!f.stopped)cpu.step();
#endif
        unsigned hash=2166136261u;for(auto b:f.memory){hash^=b;hash*=16777619u;}
        std::cout<<"FAULT {\"id\":"<<cases++<<",\"opcode\":"<<op<<",\"policy\":"<<policy
            <<",\"address\":"<<address<<",\"endian\":"<<endian<<",\"tlb_readonly\":"<<permission
            <<",\"partial\":0,\"regs\":"<<regs(cpu)<<",\"cpsr\":"<<cpu.get_cpsr()
            <<",\"count\":"<<cpu.get_num_instruction_executed()-prior<<",\"memory_hash\":"<<hash
            <<",\"calls\":"<<f.calls<<",\"faults\":"<<f.faults<<",\"events\":[";
        for(unsigned i=0;i<f.events.size();++i){if(i)std::cout<<',';std::cout<<f.events[i];}
        std::cout<<"],\"memory_changes\":[";
        bool first=true;for(unsigned i=0;i<f.memory.size();++i)if(f.memory[i]!=before[i]) {
            if(!first)std::cout<<',';first=false;std::cout<<'['<<i<<','<<unsigned(f.memory[i])<<']';
        }
        std::cout<<"]}\n";
    }
    return 0;
}


// The emulator accounts ARMv5/v6 BL as two separately budgeted halfwords.
// Native DynCom is the reference for that contract; Dynarmic Step treats the
// architectural long call as one instruction and cannot expose its midpoint.
// Native DynCom oracle for two halfwords followed by a directly linked
// Thumb ROM callee. The last instruction can fault through real callbacks.
static int rom_call_probe() {
    aot::thumb_direct_memory=true;
    std::cout<<"PROBE_ROM_CALLS 1\n";
    unsigned cases=0;
    for(unsigned op:{0x3201u,0x6808u,0x6008u,0xbc05u})
    for(unsigned policy=0;policy<4;++policy)for(unsigned budget=1;budget<=4;++budget)
    for(unsigned endian:{0u,0x200u})for(unsigned permission:{0u,1u,3u}) {
        r12l1::exclusive_monitor monitor(1);dyncom_core cpu(&monitor,12);
        Fixture f{cpu,std::vector<unsigned char>(65536),0x8000,policy};f.install();
        for(unsigned i=0x7000;i<0xa000;++i)f.memory[i]=(i*37+11)&255;
        const std::uint16_t caller[]={0x2207,0xf000,0xfffd};
        const std::uint16_t callee[]={static_cast<std::uint16_t>(op)};
        std::memcpy(f.memory.data()+0x1000,caller,sizeof(caller));
        std::memcpy(f.memory.data()+0x2000,callee,sizeof(callee));
        for(unsigned i=0;i<16;++i)cpu.set_reg(i,0x12340000+i);
        cpu.set_reg(0,0x87654321);cpu.set_reg(1,0x8000);cpu.set_reg(13,0x8000);
        cpu.set_pc(0x1000);cpu.set_cpsr(0xA0000030|endian);
        if(permission)cpu.set_tlb_page(0x8000,f.memory.data()+0x8000,permission==3?prot_read_write:prot_read);
#ifdef __EMSCRIPTEN__
        aot::global_registry().clear();
        aot::sibling_map targets{{0x2001,7}};
        auto parent=aot::translate_thumb_block(reinterpret_cast<const unsigned char *>(caller),sizeof(caller),0x1000,nullptr,nullptr,true,false,true,&targets);
        auto child=aot::translate_thumb_block(reinterpret_cast<const unsigned char *>(callee),sizeof(callee),0x2000,nullptr,nullptr,true,false,true);
        if(parent.bounded_direct_calls!=1||!child.entry_supported){std::cerr<<"ROM call fixture did not link\n";return 2;}
        parent.func.export_name="f_4097";child.func.export_name="f_8193";
        auto bytes=aot::build_wasm_module({parent.func,child.func},{{"env","tlb_read32",2,true},{"env","tlb_write32",3,false},
            {"env","tlb_read8",2,true},{"env","tlb_write8",3,false},{"env","tlb_read16",2,true},{"env","tlb_write16",3,false}});
        aot::stage_aot_module(std::move(bytes),"bounded-rom-call");aot::instantiate_staged_modules();aot::chaining_enabled=true;
        const auto compiled_before=eka2l1::common::performance::aot_instructions;
#endif
        const auto before=f.memory;const auto prior=cpu.get_num_instruction_executed();cpu.run(budget);
#ifdef __EMSCRIPTEN__
        if(eka2l1::common::performance::aot_instructions-compiled_before!=budget){std::cerr<<"ROM call escaped compiled runner\n";return 3;}
#endif
        unsigned hash=2166136261u;for(auto b:f.memory){hash^=b;hash*=16777619u;}
        std::cout<<"FAULT {\"id\":"<<cases++<<",\"opcode\":"<<op<<",\"policy\":"<<policy
            <<",\"address\":32768,\"endian\":"<<endian<<",\"tlb_readonly\":"<<permission
            <<",\"partial\":"<<budget<<",\"regs\":"<<regs(cpu)<<",\"cpsr\":"<<cpu.get_cpsr()
            <<",\"count\":"<<cpu.get_num_instruction_executed()-prior<<",\"memory_hash\":"<<hash
            <<",\"calls\":"<<f.calls<<",\"faults\":"<<f.faults<<",\"events\":[";
        for(unsigned i=0;i<f.events.size();++i){if(i)std::cout<<',';std::cout<<f.events[i];}
        std::cout<<"],\"memory_changes\":[";
        bool first=true;for(unsigned i=0;i<f.memory.size();++i)if(f.memory[i]!=before[i]) {
            if(!first)std::cout<<',';first=false;std::cout<<'['<<i<<','<<unsigned(f.memory[i])<<']';
        }
        std::cout<<"]}\n";
    }
    return 0;
}

static int thumb_call_probe() {
    aot::thumb_direct_memory=true;
    std::cout<<"PROBE_THUMB_CALLS 1\n";
    unsigned cases=0;
    for(std::uint16_t prefix:{0xf000,0xf001,0xf7ff})
    for(std::uint16_t suffix:{0xf800,0xf801,0xffff,0xe800,0xe802,0xeffe})
    for(unsigned start:{0x1000u,0x1002u})for(unsigned budget:{1u,2u})for(unsigned flags=0;flags<16;++flags) {
        r12l1::exclusive_monitor monitor(1);dyncom_core cpu(&monitor,12);
        Fixture f{cpu,std::vector<unsigned char>(65536),0x8000,0};f.install();
        const std::uint16_t program[]={prefix,suffix};
        std::memcpy(f.memory.data()+start,program,sizeof(program));
        for(unsigned i=0;i<16;++i)cpu.set_reg(i,0x12340000+i);
        cpu.set_pc(start);cpu.set_cpsr(0x30|(flags<<28));
#ifdef __EMSCRIPTEN__
        aot::global_registry().clear();
        auto tr=aot::translate_thumb_block(reinterpret_cast<const unsigned char *>(program),sizeof(program),start,nullptr,nullptr,true,false,true);
        if(!tr.entry_supported||!tr.complete||tr.end_address!=start+4){std::cerr<<"Long call pair did not compile completely\n";return 2;}
        tr.func.export_name="f_"+std::to_string(start|1);
        auto bytes=aot::build_wasm_module({tr.func},{{"env","tlb_read32",2,true},{"env","tlb_write32",3,false},
            {"env","tlb_read8",2,true},{"env","tlb_write8",3,false},{"env","tlb_read16",2,true},{"env","tlb_write16",3,false}});
        aot::stage_aot_module(std::move(bytes),"thumb-long-call");aot::instantiate_staged_modules();aot::chaining_enabled=true;
        const auto compiled_before=eka2l1::common::performance::aot_instructions;
#endif
        const auto before=f.memory;
        const auto prior=cpu.get_num_instruction_executed();cpu.run(budget);
#ifdef __EMSCRIPTEN__
        if(eka2l1::common::performance::aot_instructions-compiled_before!=budget){std::cerr<<"Long call escaped compiled runner\n";return 3;}
#endif
        if(f.memory!=before||f.calls||f.faults){std::cerr<<"Long call unexpectedly accessed data memory\n";return 4;}
        std::cout<<"FAULT {\"id\":"<<cases++<<",\"opcode\":"<<(unsigned(prefix)|(unsigned(suffix)<<16))
            <<",\"policy\":"<<budget<<",\"address\":"<<start<<",\"endian\":0,\"tlb_readonly\":0,\"partial\":"<<flags
            <<",\"regs\":"<<regs(cpu)<<",\"cpsr\":"<<cpu.get_cpsr()<<",\"count\":"<<cpu.get_num_instruction_executed()-prior
            <<",\"calls\":"<<f.calls<<",\"faults\":"<<f.faults<<",\"memory_changes\":[]}\n";
    }
    return 0;
}


// Same production exclusive monitor and callback contract on native DynCom and
// generated WASM. Include failed reservations, another processor, changed data,
// predicates, short budgets, callback stops and callback-visible state.

#ifdef __EMSCRIPTEN__
static void stage_boundary_probe(const std::vector<aot::wasm_func_def> &functions,
    const std::vector<aot::wasm_import_func> &imports, const char *name) {
    if (!aot::rom_dispatch_enabled) {
        aot::stage_aot_module(aot::build_wasm_module(functions, imports), name);
    } else {
        unsigned low=UINT32_MAX;std::uint64_t high=0;
        for(const auto &fn:functions) {
            const auto key=std::stoul(fn.export_name.substr(2));
            low=std::min(low,static_cast<unsigned>(key)&~4095u);
            high=std::max(high,(std::uint64_t(key)+4096)&~std::uint64_t{4095});
        }
        if(high>UINT32_MAX||high<=low)std::abort();
        auto map=std::make_shared<aot::rom_dispatch_map>(low,high-low,functions.size());
        for(unsigned i=0;i<functions.size();++i)
            if(!map->insert(std::stoul(functions[i].export_name.substr(2)),i))std::abort();
        auto bytes=aot::build_rom_dispatch_module(functions,imports,*map);
        if(bytes.empty())std::abort();
        aot::stage_aot_module(std::move(bytes),name,map);
    }
    aot::instantiate_staged_modules();aot::chaining_enabled=true;
}
#endif

static int arm_exclusive_probe() {
    aot::arm_exclusive_memory = true;
    unsigned cases = 0;
    for (unsigned region : {0u,1u}) for (unsigned shape=0; shape<6; ++shape)
    for (unsigned cond : {0u,1u,14u}) for (unsigned flags : {0u,0x40000000u})
    for (unsigned reservation=0; reservation<4; ++reservation)
    for (unsigned budget : {1u,2u,3u}) for (unsigned policy=0; policy<5; ++policy) {
        r12l1::exclusive_monitor monitor(2); dyncom_core cpu(&monitor,12);
        std::vector<unsigned char> memory(65536,0);
        std::uint32_t initial=0x12345678;std::memcpy(memory.data()+0x8000,&initial,4);
        const auto ld=(cond<<28)|0x01912f9f, st=(cond<<28)|0x01813f90;
        std::vector<std::uint32_t> words = shape==0 ? std::vector<std::uint32_t>{ld,st,0xe2844001}
            : shape==1 ? std::vector<std::uint32_t>{st,ld,st}
            : shape==2 ? std::vector<std::uint32_t>{(cond<<28)|0x01911f9f,0xe2844001,0xe2855001}
            : shape==3 ? std::vector<std::uint32_t>{(cond<<28)|0x01811f90,0xe2844001,0xe2855001}
            : shape==4 ? std::vector<std::uint32_t>{0xe2944001,ld,st}
            : std::vector<std::uint32_t>{ld,0xe2944001,st};
        std::memcpy(memory.data()+0x1000,words.data(),12);
        cpu.read_code=[&](unsigned a,unsigned *v){if(a>memory.size()-4)return false;std::memcpy(v,memory.data()+a,4);return true;};
        cpu.system_call_handler=[](unsigned){};
        std::vector<std::string> events; bool record=false;
        auto observe=[&](unsigned a,bool write) {
            if (!record) return;
            events.push_back(std::string("{\"write\":")+(write?"true":"false")+",\"address\":"+std::to_string(a)+",\"regs\":"+regs(cpu)+",\"cpsr\":"+std::to_string(cpu.get_cpsr())+"}");
            if ((policy==1&&!write)||(policy==2&&write)) cpu.stop();
            if (policy==3) cpu.set_reg(4,0xdecafbad);
        };
        monitor.read_32bit=[&](core *,unsigned a,unsigned *v) {
            observe(a,false); if(policy==4&&record)return false;
            if(a>memory.size()-4)return false;std::memcpy(v,memory.data()+a,4);return true;
        };
        monitor.write_32bit=[&](core *,unsigned a,unsigned v,unsigned expected)->std::int32_t {
            observe(a,true);if((policy==4&&record)||a>memory.size()-4)return 0;
            unsigned current;std::memcpy(&current,memory.data()+a,4);
            if(current!=expected)return 0;std::memcpy(memory.data()+a,&v,4);return 1;
        };
        if(reservation) monitor.exclusive_read32(&cpu,reservation==2?0x8004:0x8000);
        if(reservation==3) memory[0x8000]^=0x55;
        monitor.restore(1,monitor.snapshot(0));
        for(unsigned i=0;i<16;++i)cpu.set_reg(i,0x98760000+i);
        cpu.set_reg(0,0xaabbccdd);cpu.set_reg(1,0x8000);cpu.set_pc(0x1000);cpu.set_cpsr(flags|0xd0);
        record=true;
#ifdef __EMSCRIPTEN__
        aot::global_registry().clear();std::vector<aot::wasm_func_def> functions;
        for(unsigned i=0;i<3;++i) {
            auto tr=aot::translate_arm_block(reinterpret_cast<const unsigned char *>(words.data()+i),(3-i)*4,0x1000+i*4,nullptr,nullptr,true,false,true,region);
            if(!tr.entry_supported||!tr.complete){std::cerr<<"Exclusive instruction rejected\n";return 2;}
            tr.func.export_name="f_"+std::to_string(0x1000+i*4);functions.push_back(std::move(tr.func));
        }
        stage_boundary_probe(functions,{{"env","tlb_read32",2,true},{"env","tlb_write32",3,false},
            {"env","tlb_read8",2,true},{"env","tlb_write8",3,false},{"env","tlb_read16",2,true},{"env","tlb_write16",3,false},
            {"env","arm_exclusive",2,false}},"exclusive-probe");
        const auto compiled_before=eka2l1::common::performance::aot_instructions;
#endif
        const auto before=memory;cpu.run(budget);const auto count=cpu.get_num_instruction_executed();
#ifdef __EMSCRIPTEN__
        if (eka2l1::common::performance::aot_instructions-compiled_before!=count){std::cerr<<"Exclusive probe used interpreter\n";return 3;}
#endif
        unsigned hash=2166136261u;for(auto b:memory){hash^=b;hash*=16777619u;}
        auto r0=monitor.snapshot(0),r1=monitor.snapshot(1);
        std::cout<<"FAULT {\"id\":"<<cases++<<",\"region\":"<<region<<",\"shape\":"<<shape<<",\"condition\":"<<cond<<",\"flags\":"<<flags
            <<",\"reservation\":"<<reservation<<",\"budget\":"<<budget<<",\"policy\":"<<policy
            <<",\"regs\":"<<regs(cpu)<<",\"cpsr\":"<<cpu.get_cpsr()<<",\"count\":"<<count
            <<",\"memory_hash\":"<<hash<<",\"monitor\":["<<r0.address<<','<<r0.value[0]<<','<<r0.value[1]<<','<<r1.address<<','<<r1.value[0]<<','<<r1.value[1]<<"],\"events\":[";
        for(unsigned i=0;i<events.size();++i){if(i)std::cout<<',';std::cout<<events[i];}
        std::cout<<"],\"memory_changes\":[";bool first=true;
        for(unsigned i=0;i<memory.size();++i)if(memory[i]!=before[i]){if(!first)std::cout<<',';first=false;std::cout<<'['<<i<<','<<unsigned(memory[i])<<']';}
        std::cout<<"]}\n";
    }
    return 0;
}

// Generated SVC and continuation against native DynCom, including kernel-visible
// state and changes to budgets, flags, PC, IRQ and exclusive reservations.
static int compiled_svc_probe() {
    aot::compiled_svc_enabled = true;
    unsigned cases=0;
    for(unsigned thumb:{0u,1u}) for(unsigned region:{0u,1u})
    for(unsigned shape:{0u,1u,2u}) for(unsigned page:{0u,1u})
    for(unsigned cond:{0u,1u,14u}) for(unsigned z:{0u,1u}) {
        if(thumb && cond!=14)continue;
        for(unsigned budget:{1u,2u,3u,4u,8u,12u}) for(unsigned policy=0;policy<9;++policy) {
            r12l1::exclusive_monitor monitor(2);dyncom_core cpu(&monitor,12);
            auto *state=matched_kernel_access::state(cpu);
            std::vector<unsigned char> memory(65536,0);
            const unsigned width=thumb?2:4;
            const unsigned start=page?0x2000-width*(shape==1?2:1):0x1000;
            std::vector<unsigned> addresses;
            for(unsigned i=0;i<32;++i) {
                const unsigned a=start+i*width;addresses.push_back(a);
                if(thumb) {const std::uint16_t op=0x3401;std::memcpy(memory.data()+a,&op,2);}
                else {const unsigned op=0xe2844001;std::memcpy(memory.data()+a,&op,4);}
            }
            auto write_svc=[&](unsigned index) {
                if(thumb){const std::uint16_t op=0xdf56;std::memcpy(memory.data()+start+index*2,&op,2);}
                else {const unsigned op=(cond<<28)|0x0f123456;std::memcpy(memory.data()+start+index*4,&op,4);}
            };
            write_svc(shape==1?1:0);if(shape==2)write_svc(2);
            if(shape==1){if(thumb){const std::uint16_t op=0x3501;std::memcpy(memory.data()+start,&op,2);}else{const unsigned op=0xe2955001;std::memcpy(memory.data()+start,&op,4);}}
            // Terminal branch bounds decoding without affecting these short runs.
            if(thumb){const std::uint16_t op=0xe7fe;std::memcpy(memory.data()+start+62,&op,2);}
            else {const unsigned op=0xeafffffe;std::memcpy(memory.data()+start+124,&op,4);}
            cpu.read_code=[&](unsigned a,unsigned *v){if(a>memory.size()-4)return false;std::memcpy(v,memory.data()+a,4);return true;};
            // Mode-changing callbacks can expose the same bytes through the
            // other decoder; provide the complete ordinary memory interface.
#define SVC_ACCESS(bits,type) cpu.read_##bits##bit=[&](unsigned a,type*v){if(a>memory.size()-sizeof(type)){*v=0;return false;}std::memcpy(v,memory.data()+a,sizeof(type));return true;};cpu.write_##bits##bit=[&](unsigned a,type*v){if(a>memory.size()-sizeof(type))return false;std::memcpy(memory.data()+a,v,sizeof(type));return true;};
            SVC_ACCESS(8,std::uint8_t) SVC_ACCESS(16,std::uint16_t) SVC_ACCESS(32,std::uint32_t) SVC_ACCESS(64,std::uint64_t)
#undef SVC_ACCESS
            cpu.exception_handler=[](exception_type,unsigned){return false;};
            monitor.read_32bit=[&](core*,unsigned a,unsigned*v){if(a>memory.size()-4)return false;std::memcpy(v,memory.data()+a,4);return true;};
            monitor.exclusive_read32(&cpu,0x8000);monitor.restore(1,monitor.snapshot(0));
            for(unsigned i=0;i<16;++i)cpu.set_reg(i,0x12340000+i);
            cpu.set_reg(5,z?0xffffffff:5);cpu.set_pc(start);cpu.set_cpsr(0x10|(thumb?32:0)|(z?0x40000000:0));
            std::vector<std::string> events;
            cpu.system_call_handler=[&](unsigned number){
                const auto m=monitor.snapshot(0);
                events.push_back("{\"number\":"+std::to_string(number)+",\"regs\":"+regs(cpu)+",\"cpsr\":"+std::to_string(cpu.get_cpsr())+",\"budget\":"+std::to_string(state->NumInstrsToExecute)+",\"reservation\":"+std::to_string(m.address)+"}");
                if(policy==1)cpu.stop();
                if(policy==2){cpu.set_reg(4,0x76543210);cpu.set_cpsr(cpu.get_cpsr()^0xf0000000u);}
                if(policy==3)cpu.set_pc(start+8*width);
                if(policy==4)state->NirqSig=0;
                if(policy==5){state->NumInstrsToExecute=16;state->NirqSig=0;}
                if(policy==6)cpu.set_cpsr(cpu.get_cpsr()^32u);
                if(policy==7){state->NumInstrsToExecute=3;cpu.set_pc(start+8*width);}
                if(policy==8){memory[0x8000]=0xa5;cpu.set_reg(4,0x8000);}
            };
#ifdef __EMSCRIPTEN__
            aot::global_registry().clear();std::vector<aot::wasm_func_def> functions;
            for(unsigned i=0;i<32;++i) {
                const auto a=addresses[i];
                auto tr=thumb?aot::translate_thumb_block(memory.data()+a,(32-i)*width,a,nullptr,nullptr,true,false,true)
                    :aot::translate_arm_block(memory.data()+a,(32-i)*width,a,nullptr,nullptr,true,false,true,region);
                if(!tr.entry_supported||!tr.complete){std::cerr<<"SVC fixture rejected "<<thumb<<' '<<i<<'\n';return 2;}
                tr.func.export_name="f_"+std::to_string(a|thumb);functions.push_back(std::move(tr.func));
            }
            stage_boundary_probe(functions,{},"svc-probe");
            const auto compiled_before=eka2l1::common::performance::aot_instructions;
#endif
            cpu.run(budget);const auto count=cpu.get_num_instruction_executed();
#ifdef __EMSCRIPTEN__
            // Mode-change-without-PC-change may need a semantic fallback. Record
            // it separately; never count interpreter work as generated work.
            if(policy!=6 && budget!=1 && eka2l1::common::performance::aot_instructions-compiled_before!=count){std::cerr<<"SVC probe used interpreter\n";return 3;}
#endif
            auto m0=monitor.snapshot(0),m1=monitor.snapshot(1);
            std::cout<<"FAULT {\"id\":"<<cases++<<",\"thumb\":"<<thumb<<",\"region\":"<<region<<",\"shape\":"<<shape<<",\"page\":"<<page<<",\"condition\":"<<cond<<",\"z\":"<<z<<",\"budget\":"<<budget<<",\"policy\":"<<policy
                <<",\"regs\":"<<regs(cpu)<<",\"cpsr\":"<<cpu.get_cpsr()<<",\"count\":"<<count<<",\"remaining\":"<<state->NumInstrsToExecute<<",\"irq\":"<<state->NirqSig<<",\"data\":"<<unsigned(memory[0x8000])<<",\"monitor\":["<<m0.address<<','<<m1.address<<"],\"events\":[";
            for(unsigned i=0;i<events.size();++i){if(i)std::cout<<',';std::cout<<events[i];}
            std::cout<<"]}\n";
        }
    }
    return 0;
}

// Native DynCom oracle for register exchange, including the architectural
// Thumb PC read bias and capture-before-LR-write for register calls.
static int thumb_exchange_probe() {
    unsigned cases=0;
    for(bool direct:{false,true})for(unsigned rm=0;rm<16;++rm)
    for(bool link:{false,true})for(unsigned base:{0x1000u,0x1002u})
    for(unsigned prefix:{0u,1u,4u})for(unsigned low=0;low<4;++low)
    for(unsigned budget=0;budget<=prefix+1;++budget) {
        aot::thumb_direct_memory=direct;
        r12l1::exclusive_monitor monitor(1);dyncom_core cpu(&monitor,12);
        std::vector<unsigned char> memory(65536);
        std::vector<std::uint16_t> code(prefix,0x463f); // MOV r7,r7, no flag change
        code.push_back(static_cast<std::uint16_t>(0x4700|(rm<<3)|(link?0x80:0)));
        std::memcpy(memory.data()+base,code.data(),code.size()*2);
        cpu.read_code=[&](unsigned a,unsigned *v){if(a>memory.size()-4)return false;std::memcpy(v,memory.data()+a,4);return true;};
        cpu.exception_handler=[](exception_type,unsigned){return false;};
        for(unsigned i=0;i<15;++i)cpu.set_reg(i,0x3000+16*i+low);
        cpu.set_pc(base);cpu.set_cpsr(0xa0000030);
#ifdef __EMSCRIPTEN__
        aot::global_registry().clear();
        auto tr=aot::translate_thumb_block(memory.data()+base,code.size()*2,base,nullptr,nullptr,true,false,true);
        if(!tr.entry_supported||!tr.complete){std::cerr<<"Exchange fixture rejected\n";return 2;}
        tr.func.export_name="f_"+std::to_string(base|1);
        auto bytes=aot::build_wasm_module({tr.func},{});
        aot::stage_aot_module(std::move(bytes),"thumb-exchange-probe");aot::instantiate_staged_modules();aot::chaining_enabled=true;
        const auto before=eka2l1::common::performance::aot_instructions;
#endif
        cpu.run(budget);
#ifdef __EMSCRIPTEN__
        if(eka2l1::common::performance::aot_instructions-before!=budget){std::cerr<<"Exchange fixture interpreted\n";return 3;}
#endif
        std::cout<<"FAULT {\"id\":"<<cases++<<",\"direct\":"<<direct<<",\"rm\":"<<rm<<",\"link\":"<<link<<",\"base\":"<<base<<",\"prefix\":"<<prefix<<",\"low\":"<<low<<",\"budget\":"<<budget<<",\"regs\":"<<regs(cpu)<<",\"cpsr\":"<<cpu.get_cpsr()<<",\"count\":"<<cpu.get_num_instruction_executed()<<"}\n";
    }
    return 0;
}

int main(int argc, char **argv){
    if(argc>1 && std::strcmp(argv[argc-1],"--rom-dispatch")==0) {
        aot::rom_dispatch_enabled=true;--argc;
    }
    std::cout<<"PROBE_ROM_DISPATCH "<<aot::rom_dispatch_enabled<<"\n";
    if(argc>1 && std::strncmp(argv[argc-1],"--unsafe-code=",14)==0) {
        const std::string value(argv[argc-1]+14);
        if(value!="0" && value!="1" && value!="2" && value!="3")return 1;
        eka2l1::common::code_tracking::unsafe_code_mode=std::stoi(value);--argc;
    }
    std::cout<<"PROBE_UNSAFE_CODE "<<eka2l1::common::code_tracking::unsafe_code_mode<<"\n";
    if(argc>1 && std::strncmp(argv[argc-1],"--leaf-features=",16)==0) {
        const std::string value(argv[argc-1]+16);
        if(value.empty() || value.size()>3 || value.find_first_not_of("0123456789")!=std::string::npos || std::stoi(value)>255){std::cerr<<"Invalid leaf feature policy\n";return 1;}
        aot::leaf_features=static_cast<unsigned>(std::stoi(value));--argc;
    }
    std::cout<<"PROBE_LEAF_FEATURES "<<aot::leaf_features<<"\n";

    if(argc>1 && std::strncmp(argv[argc-1],"--exit-census=",14)==0) {
        const std::string value(argv[argc-1]+14);
        if(value!="0" && value!="1"){std::cerr<<"Invalid exit census policy\n";return 1;}
        aot::exit_census::enabled=value=="1";--argc;
    }
    std::cout<<"PROBE_EXIT_CENSUS "<<aot::exit_census::enabled<<"\n";
    if(argc>1 && std::strncmp(argv[argc-1],"--predicated-leaves=",20)==0) {
        const std::string value(argv[argc-1]+20);
        if(value!="0" && value!="1"){std::cerr<<"Invalid leaf predication policy\n";return 1;}
        aot::predicated_leaves=value=="1";--argc;
    }
    std::cout<<"PROBE_PREDICATED_LEAVES "<<aot::predicated_leaves<<"\n";
    if(argc>1 && std::strncmp(argv[argc-1],"--execution-limits=",19)==0) {
        if(!aot::parse_execution_limits(argv[argc-1]+19)){std::cerr<<"Invalid execution limits\n";return 1;}
        --argc;
    }
    std::cout<<"PROBE_LIMITS "<<aot::execution_limits_text()<<"\n";
    if (argc > 1 && std::strncmp(argv[argc-1],"--tlb-hash=",11) == 0) {
        const std::string value(argv[argc-1]+11);
        if(value!="0" && value!="1") {std::cerr<<"Invalid TLB index policy\n";return 1;}
        r12l1::dyncom_folded_tlb=value=="1";
        --argc;
    }
    std::cout << "PROBE_TLB_HASH " << r12l1::dyncom_folded_tlb << "\n";
    if (argc > 1 && std::strncmp(argv[argc-1],"--code-compare=",15) == 0) {
        const std::string value(argv[argc-1]+15);
        if(value!="0" && value!="1" && value!="2" && value!="3" && value!="4") {std::cerr<<"Invalid comparison policy\n";return 1;}
        aot::code_compare_mode=static_cast<unsigned>(value[0]-'0');
        --argc;
    }
    std::cout << "PROBE_COMPARE " << aot::code_compare_mode << "\n";
    if (argc > 1 && std::strncmp(argv[argc-1],"--code-lookup=",14) == 0) {
        const std::string value(argv[argc-1]+14);
        if(value!="0" && value!="1") {std::cerr<<"Invalid lookup policy\n";return 1;}
        aot::code_lookup_outline=value=="1";
        --argc;
    }
    std::cout << "PROBE_LOOKUP " << aot::code_lookup_outline << "\n";
    if (argc > 1 && std::strncmp(argv[argc-1],"--code-write-protect=",21) == 0) {
        const std::string value(argv[argc-1]+21);
        if(value!="0" && value!="1") {std::cerr<<"Invalid code write protection policy\n";return 1;}
#if defined(__EMSCRIPTEN__) && !defined(EKA2L1_WASM_CODE_WRITE_PROTECTION)
        if(value=="1") {std::cerr<<"Write protection build required\n";return 1;}
#endif
        eka2l1::common::code_tracking::protect_writes=value=="1";
        --argc;
    }
    std::cout << "PROBE_WRITE_PROTECT " << eka2l1::common::code_tracking::protect_writes << "\n";
    if (argc > 1 && std::string(argv[argc-1]) == "--compiled-memory-misses") {
        aot::compiled_memory_misses = true;
        --argc;
    }
    std::cout << "PROBE_COMPILED_MEMORY_MISSES " << aot::compiled_memory_misses << "\n";
    auto ir_policy=aot::arm_ir_policy::configured;
    if(const char *mode=std::getenv("EKA2L1_AOT_IR_MODE")) {
        if(!aot::parse_arm_ir_policy(mode,ir_policy)) {std::cerr<<"Invalid IR mode\n";return 1;}
    }
    // Emscripten does not automatically import the Node process environment.
    // An explicit argument sets both the probe and production-runner policy.
    if (argc == 3) {
        const std::string argument(argv[2]), prefix("--ir-policy=");
        if (argument.compare(0,prefix.size(),prefix) != 0
            || !aot::parse_arm_ir_policy(argument.c_str()+prefix.size(),ir_policy)) {
            std::cerr << "Invalid explicit IR policy\n"; return 1;
        }
        setenv("EKA2L1_AOT_IR_MODE",argument.c_str()+prefix.size(),1);
        --argc;
    }
    std::cout << "PROBE_POLICY " << static_cast<int>(ir_policy) << "\n";
    const bool ir_disabled=ir_policy==aot::arm_ir_policy::disabled || ir_policy==aot::arm_ir_policy::invariant_reads || ir_policy==aot::arm_ir_policy::invariant_writes || ir_policy==aot::arm_ir_policy::budget_chunks || ir_policy==aot::arm_ir_policy::write_budget_chunks || ir_policy==aot::arm_ir_policy::deferred_chunk_counts;
    const bool interpreter=argc==2 && (std::string(argv[1])=="--interpreter" || std::string(argv[1])=="--region-spans-interpreter" || std::string(argv[1])=="--entry-budget-interpreter");
    eka2l1::common::performance::enabled=true;
    eka2l1::common::performance::phase=2;
    eka2l1::log::filterings=std::make_unique<eka2l1::log_filterings>();
    eka2l1::log::filterings->reset_all(spdlog::level::off);
    if(argc==2 && std::string(argv[1])=="--thumb-exchange") return thumb_exchange_probe();
    if(argc==2 && std::string(argv[1])=="--compiled-svc") return compiled_svc_probe();
    if(argc==2 && std::string(argv[1])=="--arm-exclusive") return arm_exclusive_probe();
    if(argc==2 && std::string(argv[1])=="--rom-calls")return rom_call_probe();
    if(argc==2 && std::string(argv[1])=="--thumb-calls")return thumb_call_probe();
    if(argc==2 && std::string(argv[1])=="--thumb-memory")return thumb_memory_fault_probe(true);
    if(argc==2 && std::string(argv[1])=="--thumb-memory-control")return thumb_memory_fault_probe(false);
    const bool arm_leaf_memory_control=argc==2 && std::string(argv[1])=="--arm-leaf-memory-control";
    const bool arm_leaf_memory=arm_leaf_memory_control || (argc==2 && std::string(argv[1])=="--arm-leaf-memory");
    aot::arm_direct_memory=arm_leaf_memory && !arm_leaf_memory_control;
    std::cout<<"PROBE_ARM_LEAF_MEMORY "<<aot::arm_direct_memory<<"\n";
    const bool invariant_write_remap=argc==2 && std::string(argv[1])=="--invariant-write-remap";
    const bool invariant_remap=invariant_write_remap || (argc==2 && std::string(argv[1])=="--invariant-remap");
    const bool read_spans=argc==2 && std::string(argv[1])=="--read-spans";
    const bool wide_snapshots=argc==2 && std::string(argv[1])=="--wide-snapshots";
    const bool region_ir=argc==2 && std::string(argv[1])=="--region-ir";
    const bool ir_call_short=argc==2 && std::string(argv[1])=="--ir-calls-short";
    const bool preserve_inner=argc==2 && std::string(argv[1])=="--preserve-inner";
    const bool literal_pc_veneers=argc==2 && std::string(argv[1])=="--literal-pc-veneers";
    const bool tail_prefixes=argc==2 && std::string(argv[1])=="--tail-prefixes";
    const bool branch_veneers=argc==2 && std::string(argv[1])=="--branch-veneers";
    const bool prefix_calls=preserve_inner || (argc==2 && std::string(argv[1])=="--prefix-calls");
    const bool expanded_calls=argc==2 && std::string(argv[1])=="--expanded-calls";
    const bool predicated_calls=argc==2 && std::string(argv[1])=="--predicated-calls";
    const bool ir_calls=literal_pc_veneers || tail_prefixes || branch_veneers || prefix_calls || expanded_calls || predicated_calls || ir_call_short || (argc==2 && std::string(argv[1])=="--ir-calls");
    const bool ir_long=argc==2 && std::string(argv[1])=="--ir-long";
    const bool ir_conditions=argc==2 && std::string(argv[1])=="--ir-conditions";
    const bool ir_flags=argc==2 && std::string(argv[1])=="--ir-flags";
    const bool ir_recipes=argc==2 && std::string(argv[1])=="--ir-recipes";
    const bool ir_short=argc==2 && std::string(argv[1])=="--ir-short";
    const bool ir_addressing=argc==2 && std::string(argv[1])=="--ir-addressing";
    const bool ir_wide=argc==2 && std::string(argv[1])=="--ir-wide";
    const bool ir_memory=ir_long || ir_conditions || ir_calls || ir_flags || ir_recipes || ir_short || ir_addressing || ir_wide || (argc==2 && std::string(argv[1])=="--ir-memory");
    const bool ir_memory_chain=argc==2 && std::string(argv[1])=="--ir-memory-chain";
    const bool ir_segments=ir_memory || (argc==2 && std::string(argv[1])=="--ir-segments");
    const bool region_block_spans=region_ir || (argc==2 && std::string(argv[1])=="--region-block-spans");
    const bool region_spans=ir_memory_chain || region_block_spans || (argc==2 && (std::string(argv[1])=="--region-spans" || std::string(argv[1])=="--region-spans-interpreter"));
    const bool three_instructions=read_spans || wide_snapshots;
    const unsigned instruction_count=arm_leaf_memory?3:ir_long?135:invariant_remap||ir_recipes||ir_flags||ir_conditions?7:ir_segments||region_spans?5:three_instructions?3:2;
    const unsigned execution_count=ir_calls?9:ir_short?4:instruction_count;
    const bool deferred=invariant_remap || ir_segments || region_spans || three_instructions || (argc==2 && (std::string(argv[1])=="--deferred" || std::string(argv[1])=="--entry-budget-deferred"));
    const bool entry_budget = arm_leaf_memory || invariant_remap || ir_segments || region_spans || three_instructions || (argc==2 && (std::string(argv[1])=="--entry-budget" || std::string(argv[1])=="--entry-budget-interpreter" || std::string(argv[1])=="--entry-budget-deferred"));
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
    if(ir_memory) instructions={0xe5910000,0xe5810000,0xe8b1000d,0xe8a1000d,0xe891000d,0xe881000d};
    if(ir_addressing) instructions={0xe5d10000,0xe5c10000,0xe1d100b0,0xe1c100b0,0xe1d100d0,0xe1d100f0,
        0xe7910106,0xe7810106,0xe7d10106,0xe7c10106,0xe19100b6,0xe18100b6,0xe19100d6,0xe19100f6};
    if(ir_calls || ir_conditions || ir_long) instructions={0xe5910000,0xe5810000};
    if(invariant_remap) instructions={0xe5d28000,0xe8920300}; // LDRB / LDM via an unproved root
    const std::vector<unsigned> addresses=literal_pc_veneers
        ? std::vector<unsigned>{0x8010u,0x8ff8u,0x8ffcu}
        : invariant_remap
        ? std::vector<unsigned>{0x8000u,0x8ff0u}
        : region_spans
        ? std::vector<unsigned>{0x8000u,0x8ff0u,0x8ff4u}
        : read_spans
        ? std::vector<unsigned>{0x8000u,0x8ff8u,0x8ffcu}
        : std::vector<unsigned>{0x8000u,0x8ffdu,0x8ffcu};
    const std::vector<unsigned> predicates=(literal_pc_veneers || tail_prefixes || branch_veneers || ir_conditions || predicated_calls || expanded_calls || prefix_calls) ? std::vector<unsigned>{0,1,2,3,4,5,6,7,8,9,10,11,12,13} : std::vector<unsigned>{14};
    const std::vector<unsigned> initial_flags=(literal_pc_veneers || tail_prefixes || branch_veneers || ir_conditions || predicated_calls || expanded_calls || prefix_calls) ? std::vector<unsigned>{0,5,10,15} : std::vector<unsigned>{10};
    for(unsigned predicate:predicates)for(unsigned flags:initial_flags)
    for(unsigned op:instructions)for(unsigned policy=0;policy<4;++policy)
    for(unsigned address:addresses)for(unsigned endian:{0u,0x200u})for(unsigned permission:arm_leaf_memory?std::vector<unsigned>{0u,1u,3u}:std::vector<unsigned>{0u,1u})for(unsigned partial=0;partial<(!invariant_remap && !region_spans && (op&0x0e000000u)==0x08000000u?2u:1u);++partial){
#if defined(__EMSCRIPTEN__) || defined(EKA_MATCHED_REFERENCE)
        r12l1::exclusive_monitor monitor(1); dyncom_core cpu(&monitor,12);
        if(cpu.mem_cache()->folded_index!=r12l1::dyncom_folded_tlb) {std::cerr<<"TLB index not selected\n";return 4;}
#else
        dynarmic_exclusive_monitor monitor(1); dynarmic_core cpu(&monitor);
#endif
        const unsigned run_count=ir_call_short ? (address==0x8000 ? 1u : (address&3) ? 2u : 3u) : execution_count;
        // Span fixtures allow the first load, then fault the second unless its
        // page is mapped. The 0x8ffc case crosses into an unmapped next page.
        Fixture f{cpu, std::vector<unsigned char>(65536),address,policy,region_spans?3u:read_spans?1u:partial};f.install();
        for(unsigned i=0x8000;i<0xa000;++i)f.memory[i]=(i*37+11)&255;
        // MOVS precedes the access, so exception observers see live flags/registers.
        unsigned program[135]={region_ir?0xe3a02007u:0xe3b02007u,wide_snapshots?0xe0c54796u:region_spans?0xe5910000u:op,
            wide_snapshots?op:(read_spans||region_spans)?0xe5913004u:0xeafffffeu,
            0xe5914008u,region_block_spans?op:0xe591500cu};
        if(arm_leaf_memory) program[2]=0xe2844001;
        if(ir_segments) {
            program[0]=0xe3b02007u; // flags visible to the final fault callback
            program[1]=0xe2844001u; program[2]=0xe0245000u; program[3]=0xe1a06005u;
            program[4]=op;
            if(ir_memory) {program[1]=0xe1a08004u;program[2]=0xe1a04005u;program[3]=0xe1a05008u;}
            if(ir_wide) {program[1]=0xe0c54796u;program[2]=0xe0e54896u;program[3]=0xe0a54996u;}
            if(ir_recipes) {program[1]=0xe0c54796u;program[2]=0xe0848005u;program[3]=op;program[4]=0xe3a04000u;program[5]=0xe3a05000u;program[6]=0xe3a08000u;}
            if(ir_flags) {program[1]=0xe0944005u;program[2]=0xe0b46000u;program[3]=op;
                program[4]=0xe0967005u;program[5]=0xe3a04000u;program[6]=0xe3a06000u;}
            if(ir_conditions) {program[0]=0x00944005u|(predicate<<28);program[1]=0x01a04005u|(((predicate+1)%14)<<28);
                program[2]=0xe1a06004u;program[3]=op;program[4]=0xe3a04000u;program[5]=0xe3a05000u;program[6]=0xe3a06000u;}
            if(ir_short) {program[1]=0xe1a08004u;program[2]=op;program[3]=0xe2844001u;program[4]=0xe1a05008u;}
        }
        if(ir_long) {
            for(unsigned n=0;n<135;++n)program[n]=n%2?0xe0944005u:0x20a66007u;
            program[0]=0xe1a09001u;program[1]=0xe1a01009u;
            program[96]=0xe0c54796u;program[97]=0xe0e54896u;
            program[111]=op;
        }
        if(invariant_remap) {
            const unsigned words[]={0xe3b03007u,0xe5910000u,op,0xe5914004u,0xe5915008u,0xe591600cu,0xe0847005u};
            std::memcpy(program,words,sizeof(words));
            if(invariant_write_remap) {
                program[3]=0xe5814004; program[4]=0xe5815008; program[5]=0xe581600c;
                f.remap_writable=true;
            }
            f.remap_root=address&~4095u;
            for(unsigned i=0xa000;i<0xb000;++i) f.memory[i]=(i*13+91)&255;
        }
        const unsigned callee_address=literal_pc_veneers?0x8000u:0x2000u;
        unsigned called_words[]={0xe0944005u,0xe0b46000u,op,0xe12fff1eu};
        if(ir_calls) {
            program[0]=0xe3b02007u;program[1]=0xeb0003fdu;program[2]=0xe0967005u;
            program[3]=0xe3a04000u;program[4]=0xe3a06000u;
            if(predicated_calls) {
                program[0]=0xe3a02007u;
                called_words[0]=0x00944005u|(predicate<<28);
                called_words[1]=0x00b46000u|((predicate^1)<<28);
            }
            if(expanded_calls) {
                program[0]=0xe3a02007u;
                called_words[0]=0x0a000000u|(predicate<<28); // forward join at 0x2008
                called_words[1]=op;
                called_words[2]=0xe0944005u;
            }
            if(prefix_calls) {
                program[0]=0xe3a02007u;
                called_words[0]=0xe92d4010u; // preserve caller LR as a real stack store
                called_words[1]=0x00944005u|(predicate<<28);
                called_words[2]=op;called_words[3]=0xeb0003fbu; // nested call to 0x3000
                const unsigned nested[]={0xe28aa001u,0xe2899001u,preserve_inner?0xe12fff1eu:0xeafffffeu};
                std::memcpy(f.memory.data()+0x3000,nested,sizeof(nested));
            }
            if(branch_veneers || tail_prefixes) {
                program[0]=0xe3a02007u;
                const unsigned target[]={0x00944005u|(predicate<<28),0x00b46000u|((predicate^1)<<28),op,0xe12fff1eu};
                std::memcpy(f.memory.data()+0x3000,target,sizeof(target));
                called_words[0]=0xea0003feu; // B 0x3000; following words are unreachable.
                if(tail_prefixes) {
                    called_words[0]=0x00944005u|(predicate<<28);
                    called_words[1]=0xe1a06004u;
                    called_words[2]=0xe2877001u;
                    called_words[3]=0xea0003fbu; // B 0x3000 after the register prefix.
                }
            }
            if(literal_pc_veneers) {
                program[0]=0xe3a02007u;program[1]=0xeb001bfdu; // BL 0x8000
                called_words[0]=0xe59ff000u|(address-0x8008u);
                const unsigned target[]={0x00944005u|(predicate<<28),0x00b46000u|((predicate^1)<<28),op,0xe12fff1eu};
                std::memcpy(f.memory.data()+0x3000,target,sizeof(target));
                const unsigned literal=endian?0x00300000u:0x3000u;
                std::memcpy(f.memory.data()+address,&literal,4);
            }
            std::memcpy(f.memory.data()+callee_address,called_words,sizeof(called_words));
        }
        std::memcpy(f.memory.data()+0x1000,program,(ir_long?135u:7u)*sizeof(unsigned));
        for(unsigned i=0;i<16;++i)cpu.set_reg(i,0x12340000+i);
        cpu.set_reg(0,0x87654321);cpu.set_reg(1,address);cpu.set_pc(0x1000);cpu.set_cpsr((flags<<28)|0x10|endian);
        if(ir_conditions){cpu.set_reg(4,0x7fffffffu);cpu.set_reg(5,1);}
        if(ir_addressing)cpu.set_reg(6,1);
        if(prefix_calls){cpu.set_reg(13,0xb020);cpu.set_tlb_page(0xb000,f.memory.data()+0xb000,prot_read_write);}
        if(invariant_remap)cpu.set_reg(2,0xb000);
        // Read-only TLB: permitted control for loads, denied mapping for stores.
        // Unmapped cases always exercise failure callbacks. Partial cases allow
        // the first transferred word before failing subsequent accesses.
        if(permission || invariant_remap)cpu.set_tlb_page(address&~4095u,f.memory.data()+(address&~4095u),
            (permission==3 || (invariant_remap && !permission))?prot_read_write:prot_read);
#if defined(__EMSCRIPTEN__) && !defined(EKA_MATCHED_REFERENCE)
        if(!interpreter) {
        // Every suffix is a real runner entry after a deferred access/Step.
        std::vector<aot::wasm_func_def> functions;
        aot::leaf_resolver call_resolver=[&](std::uint32_t pc) {
            const auto *begin=reinterpret_cast<const std::uint8_t *>(called_words);
            if(preserve_inner && pc==0x3000)return std::vector<std::uint8_t>(f.memory.begin()+0x3000,f.memory.begin()+0x300c);
            return pc==callee_address ? std::vector<std::uint8_t>(begin,begin+sizeof(called_words)) : std::vector<std::uint8_t>{};
        };
        for(unsigned n=0;n<instruction_count;++n) {
            auto translated=aot::translate_arm_block(reinterpret_cast<unsigned char*>(program+n),
                (instruction_count-n)*4,0x1000+n*4,nullptr,nullptr,true,false,true,!arm_leaf_memory,ir_calls?&call_resolver:nullptr,deferred,ir_policy);
            if(literal_pc_veneers && n==0 && (translated.dependencies.size()!=unsigned(aot::predicated_leaves && (aot::leaf_features&128)) ||
                (!translated.dependencies.empty() && (translated.dependencies[0].bytes.size()!=4 || translated.dependencies[0].address!=callee_address)))) {
                std::cerr<<"Literal PC veneer fusion selection mismatch\n";return 4;
            }
            if(branch_veneers && n==0 && (translated.dependencies.size()!=unsigned(aot::predicated_leaves && (aot::leaf_features&32)) ||
                (!translated.dependencies.empty() && translated.dependencies[0].bytes.size()!=4))) {
                std::cerr<<"Branch veneer fusion selection mismatch\n";return 4;
            }
            if(tail_prefixes && n==0 && (translated.dependencies.size()!=unsigned(aot::predicated_leaves && (aot::leaf_features&64)) ||
                (!translated.dependencies.empty() && translated.dependencies[0].bytes.size()!=16))) {
                std::cerr<<"Tail-prefix fusion selection mismatch\n";return 4;
            }
            if(prefix_calls && n==0 && translated.dependencies.size()!=unsigned(aot::predicated_leaves && (aot::leaf_features&8) && !(preserve_inner && (aot::leaf_features&16)))) {
                std::cerr<<"Call-prefix fusion selection mismatch\n";return 4;
            }
            if(expanded_calls && n==0 && translated.dependencies.size()!=unsigned(aot::predicated_leaves && (aot::leaf_features&4))) {
                std::cerr<<"Expanded call fusion selection mismatch\n";return 4;
            }
            if(predicated_calls && n==0 && translated.dependencies.size()!=unsigned(aot::predicated_leaves)) {
                std::cerr<<"Predicated call fusion selection mismatch\n";return 4;
            }
            const auto checked_policy = (ir_policy == aot::arm_ir_policy::long_segments_ir || ir_policy == aot::arm_ir_policy::stack_values_ir || ir_policy == aot::arm_ir_policy::budget_gaps_ir)
                ? aot::arm_ir_policy::conditional_value_ir : ir_policy;
            if(ir_long && n==0 && translated.ir_max_segment_length != (ir_policy==aot::arm_ir_policy::long_segments_ir?128u:32u)) {
                std::cerr<<"Long fault fixture did not select expected segment cap: policy="<<int(ir_policy)<<" max="<<translated.ir_max_segment_length<<" segments="<<translated.ir_segments<<" selected="<<translated.ir_segment_instructions<<"\n";return 4;
            }
            if((checked_policy==aot::arm_ir_policy::invariant_writes || (checked_policy==aot::arm_ir_policy::invariant_write_ir || checked_policy==aot::arm_ir_policy::conditional_value_ir) || checked_policy==aot::arm_ir_policy::write_budget_chunks || checked_policy==aot::arm_ir_policy::deferred_chunk_counts) && invariant_write_remap && n==0 && translated.proved_writes!=3) {std::cerr<<"Write remap proof was not selected\n";return 4;}
            if((checked_policy==aot::arm_ir_policy::invariant_write_ir || checked_policy==aot::arm_ir_policy::conditional_value_ir) && invariant_write_remap && n==0 && translated.ir_proved_writes!=3) {std::cerr<<"IR write remap proof was not used\n";return 4;}
            if(((checked_policy==aot::arm_ir_policy::invariant_read_ir || checked_policy==aot::arm_ir_policy::invariant_read_flag_ir || (checked_policy==aot::arm_ir_policy::inline_call_ir || (checked_policy==aot::arm_ir_policy::invariant_write_ir || checked_policy==aot::arm_ir_policy::conditional_value_ir))) || checked_policy==aot::arm_ir_policy::invariant_reads || checked_policy==aot::arm_ir_policy::invariant_writes || (checked_policy==aot::arm_ir_policy::invariant_write_ir || checked_policy==aot::arm_ir_policy::conditional_value_ir) || checked_policy==aot::arm_ir_policy::budget_chunks || checked_policy==aot::arm_ir_policy::write_budget_chunks || checked_policy==aot::arm_ir_policy::deferred_chunk_counts) && ((!invariant_write_remap && invariant_remap) || (region_spans && !region_block_spans)) && n==0 && !translated.proved_reads) {
                std::cerr << "Invariant read fault fixture did not select entry proof\n"; return 4;
            }
            if((checked_policy==aot::arm_ir_policy::invariant_read_ir || checked_policy==aot::arm_ir_policy::invariant_read_flag_ir || (checked_policy==aot::arm_ir_policy::inline_call_ir || (checked_policy==aot::arm_ir_policy::invariant_write_ir || checked_policy==aot::arm_ir_policy::conditional_value_ir))) && invariant_remap && !invariant_write_remap && n==0 && !translated.ir_proved_reads) {std::cerr<<"IR read remap proof was not used\n";return 4;}
            if(!ir_disabled && !ir_long && !ir_flags && !ir_calls && ir_segments && n==0 && !translated.ir_segments) {
                std::cerr << "Integer-segment fault fixture did not select the IR\n"; return 4;
            }
            // The four-load chain uses entry proofs in invariant-aware policies.
            // Require all four actual IR proof consumers rather than accepting no coverage.
            if(!ir_disabled && !ir_flags && !ir_calls && (ir_memory || ir_memory_chain) && n==0
                && !translated.ir_memory_guards && !(ir_memory_chain && translated.ir_proved_reads==4)) {
                std::cerr << "Dynamic memory fixture did not select IR guard exits\n"; return 4;
            }
            if(!ir_disabled && ir_wide && n==0 && !translated.ir_wide_products) {
                std::cerr << "Wide memory fixture did not select IR products\n"; return 4;
            }
            if(!ir_disabled && checked_policy!=aot::arm_ir_policy::inline_segments && ir_short && n==0 && !translated.ir_outlined_segments) {
                std::cerr << "Short-budget fault fixture did not select private IR fallback\n"; return 4;
            }
            if(ir_recipes && checked_policy==aot::arm_ir_policy::outlined_recipes && n==0
                && translated.ir_cold_values<=translated.ir_cold_halves) {
                std::cerr<<"General exit recipes were not selected\n";return 4;
            }
            if((checked_policy==aot::arm_ir_policy::disabled && (translated.ir_segments || translated.func.outlined_callee))
                || (checked_policy==aot::arm_ir_policy::inline_segments && translated.ir_outlined_segments)) {
                std::cerr<<"IR mode selection was ignored\n";return 4;
            }
            if(ir_flags && (checked_policy==aot::arm_ir_policy::invariant_read_flag_ir || checked_policy==aot::arm_ir_policy::inline_call_ir || (checked_policy==aot::arm_ir_policy::invariant_write_ir || checked_policy==aot::arm_ir_policy::conditional_value_ir)) && n==0
                && (translated.ir_flag_instructions!=4 || !translated.ir_memory_guards)) {
                std::cerr<<"Flag IR fault snapshot was not selected\n";return 4;
            }
            if(ir_conditions && checked_policy==aot::arm_ir_policy::conditional_value_ir && n==0
                && (translated.ir_conditional_instructions!=2 || !translated.ir_memory_guards)) {
                std::cerr<<"Conditional fault snapshot was not selected\n";return 4;
            }
            if(ir_calls && (checked_policy==aot::arm_ir_policy::inline_call_ir || (checked_policy==aot::arm_ir_policy::invariant_write_ir || checked_policy==aot::arm_ir_policy::conditional_value_ir)) && n==0
                && (translated.ir_inline_transfers!=2 || !translated.ir_memory_guards)) {
                std::cerr<<"Inline-call fault snapshot was not selected\n";return 4;
            }
            functions.push_back(std::move(translated.func));
        }
        if(ir_calls) for(unsigned n=0;n<4;++n) {
            auto translated=aot::translate_arm_block(reinterpret_cast<const unsigned char *>(called_words+n),
                (4-n)*4,callee_address+n*4,nullptr,nullptr,true,false,true,true,preserve_inner?&call_resolver:nullptr,true,ir_policy);
            if(preserve_inner && n==0 && translated.dependencies.size()!=1){std::cerr<<"Preserved inner leaf not selected\n";return 4;}
            functions.push_back(std::move(translated.func));
        }
        if(literal_pc_veneers || prefix_calls || branch_veneers || tail_prefixes)for(unsigned n=0;n<((literal_pc_veneers || branch_veneers || tail_prefixes)?4u:3u);++n) {
            auto translated=aot::translate_arm_block(f.memory.data()+0x3000+n*4,(((literal_pc_veneers || branch_veneers || tail_prefixes)?4u:3u)-n)*4,0x3000+n*4,nullptr,nullptr,true,false,true,true,nullptr,true,ir_policy);
            functions.push_back(std::move(translated.func));
        }
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
        if(entry_budget) cpu.run(run_count); else cpu.step();
#else
        // Native uses individual Step calls. Honor a callback stop across
        // that harness loop, just as one production Run does on WASM.
        for(unsigned n=1;n<run_count && !f.stopped;++n) cpu.step();
#endif
#endif
#if defined(__EMSCRIPTEN__) && !defined(EKA_MATCHED_REFERENCE)
        const auto compiled=eka2l1::common::performance::aot_instructions-prior_compiled;
        if(deferred && compiled<(ir_calls?execution_count:instruction_count))++deferred_cases;
        if (aot::compiled_memory_misses && (std::string(argv[1]) == "--entry-budget-deferred" || wide_snapshots) && compiled != instruction_count) {
            std::cerr << "Compiled memory miss escaped to interpreter at case " << cases << '\n'; return 3;
        }
        if(!interpreter && (arm_leaf_memory ? (compiled<2 || compiled>3) : ir_long ? (compiled<111 || compiled>135) : invariant_remap ? (compiled<(endian?1u:2u) || compiled>7) : ir_call_short ? compiled!=run_count : literal_pc_veneers ? (compiled<2 || compiled>9) : prefix_calls ? (compiled<2 || compiled>9) : expanded_calls ? (compiled<3 || compiled>9) : ir_calls ? (compiled<4 || compiled>9) : (ir_recipes || ir_flags || ir_conditions) ? (compiled<3 || compiled>7) : ir_short ? (compiled<2 || compiled>4) : ir_segments ? (compiled<4 || compiled>5) : region_spans ? (compiled>5 || (permission && !endian && address<=0x8ff0 && (!region_block_spans || (op&(1u<<20))) && compiled!=5)) : three_instructions ? (compiled<(wide_snapshots?2u:1u) || compiled>3) : (compiled!=2 && !(deferred && compiled==1)))){
            std::cerr<<"Unexpected generated instruction count at case "<<cases<<'\n';return 2;
        }
#endif
        if(invariant_remap) {
            auto expected_word=[&](unsigned a) {
                unsigned value=0; std::memcpy(&value,initial_memory.data()+a,4);
                if(endian) value=((value&255)<<24)|((value&65280)<<8)|((value>>8)&65280)|(value>>24);
                return value;
            };
            if(!f.remapped || !f.faults || cpu.get_reg(0)!=expected_word(address)) {
                std::cerr<<"Remapping fixture did not preserve the first read\n";return 4;
            }
            if(!f.stopped) for(unsigned r=4;r<=6;++r) {
                const auto destination=0xa000+(address&4095u)+(r-3)*4;
                if(invariant_write_remap) {
                    unsigned stored=0;std::memcpy(&stored,f.memory.data()+destination,4);
                    unsigned expected=0x12340000+r;
                    if(endian) expected=((expected&255)<<24)|((expected&65280)<<8)|((expected>>8)&65280)|(expected>>24);
                    const auto original=address+(r-3)*4;
                    if(stored!=expected || std::memcmp(f.memory.data()+original,initial_memory.data()+original,4)) {
                        std::cerr<<"Post-callback store used stale mapping\n";return 4;
                    }
                } else if(cpu.get_reg(r)!=expected_word(destination)) {
                    std::cerr<<"Post-callback read used stale mapping\n";return 4;
                }
            }
        }
        unsigned hash=2166136261u;for(auto b:f.memory){hash^=b;hash*=16777619u;}
        std::cout<<"FAULT {\"id\":"<<cases++<<",\"opcode\":"<<op<<",\"policy\":"<<policy<<",\"address\":"<<address<<",\"endian\":"<<endian<<",\"tlb_readonly\":"<<permission<<",\"partial\":"<<(region_spans?f.partial:partial)<<",\"condition\":"<<predicate<<",\"initial_flags\":"<<flags<<",\"regs\":"<<regs(cpu)<<",\"cpsr\":"<<cpu.get_cpsr()<<",\"count\":"<<(
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
    if(deferred){std::cerr<<"Deferred cases: "<<deferred_cases<<'\n';if(!deferred_cases && !ir_call_short && !aot::compiled_memory_misses)return 3;}
#endif
}
