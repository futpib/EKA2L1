// Caller-specific stack returns preserve ordinary memory and helper exits.
static bool test_stack_returns() {
#ifdef __EMSCRIPTEN__
    using S=state_offsets;
    struct restore {
        bool enabled=arm_indirect_calls;unsigned mode=memory_experiment::mode;
        unsigned unsafe=eka2l1::common::code_tracking::unsafe_code_mode;
        ~restore(){arm_indirect_calls=enabled;memory_experiment::mode=mode;
            eka2l1::common::code_tracking::unsafe_code_mode=unsafe;g_test_mem=nullptr;g_memory_boundary={};}
    } saved;
    eka2l1::common::code_tracking::unsafe_code_mode=3;
    const std::vector<wasm_import_func> imports={{"env","tlb_read32",2,true},{"env","tlb_write32",3,false},
        {"env","tlb_read8",2,true},{"env","tlb_write8",3,false},{"env","tlb_read16",2,true},{"env","tlb_write16",3,false}};
    // Preserve the outer return in r8, with two different callee continuations.
    unsigned caller[]={0xe1a0800eu,0xeb0003fdu,0xe2800001u,0xe3a02002u,0xeb0003fau,0xe2800003u,0xe12fff18u,0xe59f70dcu};
    unsigned checks=0,independent=0,selected=0,proved_calls=0;unsigned calls[2]{};
    for(unsigned policy:{0u,2u})for(unsigned shape=0;shape<3;++shape)for(bool defer:{false,true})for(unsigned indirect=1;indirect<4;++indirect) {
        caller[1]=indirect?0xe12fff37u:0xeb0003fdu;caller[4]=indirect?0xe12fff37u:0xeb0003fau;
        const unsigned call_target=indirect==2?0x3000:indirect==3?0x2001:0x2000;
        const bool store=shape==1,proof=shape==2;
        memory_experiment::mode=policy;
        std::vector<unsigned> callee={0xe92d4010u,0xe1a04000u,store?0xe5813000u:0xe5913000u,
            0xe0800003u,0xe2522001u,0x1afffffbu,0xe8bd8010u};
        if(proof)callee={0xe92d4010u,0xe5913000u,0xe5914004u,0xe5915008u,0xe591600cu,0xe591c010u,0xe0800003u,0xe8bd8010u};
        const auto cb=reinterpret_cast<const std::uint8_t*>(caller),lb=reinterpret_cast<const std::uint8_t*>(callee.data());
        leaf_resolver resolve=[&](unsigned pc){return pc==0x1100?std::vector<std::uint8_t>{0,0x20,0,0}:pc==0x2000?std::vector<std::uint8_t>(lb,lb+(callee.size()*4)):std::vector<std::uint8_t>{};};
        std::map<unsigned,std::vector<std::uint8_t>> modules[2];
        for(unsigned variant=0;variant<2;++variant) {
            arm_indirect_calls=variant;
            for(unsigned off=0;off<sizeof(caller);off+=4) {
                auto tr=translate_arm_block(cb+off,sizeof(caller)-off,0x1000+off,nullptr,nullptr,true,false,true,true,&resolve,defer,arm_ir_policy::write_spans);
                if(!off && variant && (tr.stack_calls!=2 || tr.proved_stack_returns!=(store?0u:2u))) {
                    printf(" FAIL stack-return selection %u/%u\n",tr.stack_calls,tr.proved_stack_returns);return false;
                }
                if(!off && variant && proof)proved_calls+=tr.func.outlined_calls.size();
                selected+=tr.stack_calls;modules[variant][0x1000+off]=build_wasm_module({tr.func},imports);
            }
            for(unsigned off=0;off<(callee.size()*4);off+=4) {
                auto tr=translate_arm_block(lb+off,(callee.size()*4)-off,0x2000+off,nullptr,nullptr,true,false,true,true,nullptr,defer,arm_ir_policy::write_spans);
                modules[variant][0x2000+off]=build_wasm_module({tr.func},imports);
            }
        }
        for(unsigned flags:{0u,5u,10u,15u})for(unsigned mapping=0;mapping<3;++mapping)
        for(unsigned alias=0;alias<(store?3u:1u);++alias)for(unsigned event=0;event<4;++event) {
            test_mem memories[2];memories[0].write_code(0x1000,{cb,cb+sizeof(caller)});memories[0].write_code(0x2000,{lb,lb+(callee.size()*4)});
            memories[0].write32(0x9000,5);memories[1]=memories[0];
            alignas(8) unsigned states[2][256]{};unsigned helpers[2]{};
            std::vector<std::array<unsigned,21>> observations[2];
            for(unsigned variant=0;variant<2;++variant) {
                auto *s=states[variant];for(unsigned reg=0;reg<15;++reg)s[reg]=reg*37;
                if(indirect)s[7]=call_target;
                s[0]=1;s[1]=alias?0x8ffc:0x9000;s[2]=2;s[3]=alias==2?0x4001:0x4000;s[13]=0x9000;s[14]=0x4000;s[15]=0x1000;
                s[S::CPSR/4]=0x10|(flags<<28);s[S::MODE/4]=0x10;s[S::NIRQ/4]=1;s[S::NUM_INSTRS_TO_EXECUTE/4]=100;
                for(unsigned f=0;f<4;++f)s[test_flag_offsets[f]/4]=(flags>>(3-f))&1;
                r12l1::tlb tlb(12);std::vector<memory_experiment::binding> bindings;
                for(unsigned page:{0x8000u,0x9000u})if(!((mapping==1&&page==0x8000)||(mapping==2&&page==0x9000))) {
                    auto *host=memories[variant].data.data()+page;tlb.add(page,host,3);
                    bindings.push_back({page,static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(host)),3});
                }
                memory_experiment::view view;auto pointer=view.enter(1,1,[&]{return bindings;});
                s[S::AOT_TLB/4]=policy?static_cast<unsigned>(pointer):reinterpret_cast<std::uintptr_t>(tlb.entries);
                g_test_mem=&memories[variant];g_all_memory_helper_calls=0;
                g_memory_boundary=[&](bool after,unsigned ptr) {
                    auto *state=reinterpret_cast<unsigned*>(ptr);
                    if(after && event==1){state[S::NIRQ/4]=0;state[S::NUM_INSTRS_TO_EXECUTE/4]=0;}
                    if(after && event==2)memories[variant].write32(0x8ffc,0x4001);
                    if(after && event==3){state[14]=0x5555;state[S::CFLAG/4]=1;}
                    std::array<unsigned,21> snapshot{};std::copy(state,state+16,snapshot.begin());
                    for(unsigned f=0;f<4;++f)snapshot[16+f]=state[test_flag_offsets[f]/4];snapshot[20]=state[S::TFLAG/4];
                    observations[variant].push_back(snapshot);
                };
                for(unsigned round=0;round<128;++round) {
                    auto it=modules[variant].find(s[15]);
                    if(it==modules[variant].end()||s[S::TFLAG/4]||!s[S::NIRQ/4]||!s[S::NUM_INSTRS_TO_EXECUTE/4])break;
                    s[S::AOT_EXIT/4]=0;
                    int result=js_run_aot_wasm(it->second.data(),it->second.size(),reinterpret_cast<std::uint8_t*>(s),sizeof(states[variant]));
                    if(result<0||result>1)return false;++calls[variant];if(!result)break;
                    if(round==127){printf(" FAIL stack-return chain failed to finish\n");return false;}
                }
                helpers[variant]=g_all_memory_helper_calls;s[S::AOT_TLB/4]=0;
            }
            if(std::memcmp(states[0],states[1],sizeof(states[0]))||memories[0].data!=memories[1].data||helpers[0]!=helpers[1]||observations[0]!=observations[1]) {
                printf(" FAIL stack-return policy=%u store=%u defer=%u indirect=%u flags=%u mapping=%u alias=%u event=%u helpers=%u/%u observations=%zu/%zu\n",policy,store,defer,indirect,flags,mapping,alias,event,helpers[0],helpers[1],observations[0].size(),observations[1].size());
                for(unsigned i=0;i<256;++i)if(states[0][i]!=states[1][i])printf(" state[%u] %x/%x\n",i,states[0][i],states[1][i]);return false;
            }
            if(!event && !mapping) {
                test_mem expected;expected.write_code(0x1000,{cb,cb+sizeof(caller)});expected.write_code(0x2000,{lb,lb+(callee.size()*4)});expected.write32(0x9000,5);
                r12l1::exclusive_monitor mon(1);auto cpu=make_cpu(expected,mon);
                for(unsigned reg=0;reg<15;++reg)cpu->set_reg(reg,reg*37);
                if(indirect)cpu->set_reg(7,call_target);
                cpu->set_reg(0,1);cpu->set_reg(1,alias?0x8ffc:0x9000);cpu->set_reg(2,2);cpu->set_reg(3,alias==2?0x4001:0x4000);
                cpu->set_reg(13,0x9000);cpu->set_reg(14,0x4000);cpu->set_pc(0x1000);cpu->set_cpsr(0x10|(flags<<28));
                if(reference_to(*cpu,states[1])<0||expected.data!=memories[1].data)return false;++independent;
            }
            ++checks;
        }
    }
    // Stack-pointer changes, LR changes, escaping branches and unmatched pops
    // cannot establish the saved return's lifetime.
    arm_indirect_calls=true;caller[1]=caller[4]=0xe12fff37u;
    for(unsigned op:{0xe28dd004u,0xe28ee004u,0xeb000000u,0xeafffffdu,0xe8bd8001u,0xe8b1e000u}) {
        const unsigned callee[]={0xe92d4010u,op,0xe8bd8010u};const auto *bytes=reinterpret_cast<const std::uint8_t*>(callee);
        leaf_resolver resolve=[&](unsigned pc){return pc==0x1100?std::vector<std::uint8_t>{0,0x20,0,0}:pc==0x2000?std::vector<std::uint8_t>(bytes,bytes+sizeof(callee)):std::vector<std::uint8_t>{};};
        auto tr=translate_arm_block(reinterpret_cast<const std::uint8_t*>(caller),sizeof(caller),0x1000,nullptr,nullptr,true,false,true,true,&resolve,false,arm_ir_policy::write_spans);
        if(tr.stack_calls){printf(" FAIL unsafe stack-return accepted %x\n",op);return false;}
    }
    if(!selected||!proved_calls||calls[1]>=calls[0])return false;
    printf(" PASS stack returns: %u state/memory/callback checks, %u interpreter checks, calls %u -> %u, proof fallbacks %u\n",checks,independent,calls[0],calls[1],proved_calls);
#endif
    return true;
}

#ifdef __EMSCRIPTEN__
// Fixed work through the captured tile callee and the real compiled runner.
extern "C" EMSCRIPTEN_KEEPALIVE int tile_call_benchmark(unsigned variant, unsigned iterations) {
    if(!eka2l1::log::filterings) {
        eka2l1::log::filterings=std::make_unique<eka2l1::log_filterings>();
        eka2l1::log::filterings->reset_all(spdlog::level::off);
    }
    static test_mem memory;
    static r12l1::exclusive_monitor monitor(1);
    static auto core=make_cpu(memory,monitor);
    static memory_experiment::view view;
    static aot_func entries[2][4]{};
    static std::uintptr_t pointer=0;
    static bool ready=false;
    const unsigned pcs[]={0x1000,0x1004,0x1014,0x2000};
    if(!ready) {
        hotpath_policy=2;memory_experiment::mode=2;
        eka2l1::common::code_tracking::unsafe_code_mode=3;
        global_registry().configure_rom_index(0x1000,0x2000,true);
        const unsigned caller[]={0xe1a0800eu,0xe1a00004u,0xe1a01005u,0xe1a02006u,
            0xe12fff37u,0xe2599001u,0x1afffff9u,0xe12fff18u,0xe59f70d8u};
        // Captured 8-by-8 tile expansion at 0x700032bc; relative branches unchanged.
        const unsigned callee[]={0xe92d4ff0u,0xe2400c02u,0xe3a0b008u,0xe3a0c00fu,0xe1a0c08cu,0xe3100003u,0x12800002u,0x0a00001au,0xe4913004u,0xe25bb001u,0xe00c5083u,0xe00c61c3u,0xe00c73c3u,0xe00c85c3u,0xe00c97c3u,0xe00ca9c3u,0xe19250b5u,0xe19260b6u,0xe19270b7u,0xe19280b8u,0xe19290b9u,0xe192a0bau,0xe1866807u,0xe1887809u,0xe00c8bc3u,0xe00c9dc3u,0xe19280b8u,0xe19290b9u,0xe2800c02u,0xe18a8808u,0xe14050b2u,0xe88001c0u,0xe1c090bcu,0x1affffe5u,0xea000018u,0xe4913004u,0xe25bb001u,0xe00c5083u,0xe00c61c3u,0xe00c73c3u,0xe00c85c3u,0xe19250b5u,0xe19260b6u,0xe19270b7u,0xe19280b8u,0xe1855806u,0xe1876808u,0xe00c77c3u,0xe00c89c3u,0xe00c9bc3u,0xe00cadc3u,0xe19270b7u,0xe19280b8u,0xe19290b9u,0xe192a0bau,0xe2800c02u,0xe1877808u,0xe189880au,0xe88001e0u,0x1affffe6u,0xe8bd8ff0u};
        const auto cb=reinterpret_cast<const std::uint8_t*>(caller),lb=reinterpret_cast<const std::uint8_t*>(callee);
        leaf_resolver resolve=[&](unsigned pc){return pc==0x1100?std::vector<std::uint8_t>{0,0x20,0,0}:pc==0x2000?std::vector<std::uint8_t>(lb,lb+sizeof(callee)):std::vector<std::uint8_t>{};};
        const std::vector<wasm_import_func> imports{{"env","tlb_read32",2,true},{"env","tlb_write32",3,false},
            {"env","tlb_read8",2,true},{"env","tlb_write8",3,false},{"env","tlb_read16",2,true},{"env","tlb_write16",3,false}};
        for(unsigned v=0;v<2;++v) {
            arm_indirect_calls=v;std::vector<wasm_func_def> functions;
            for(unsigned i=0;i<4;++i) {
                auto tr=translate_arm_block(i==3?lb:cb+pcs[i]-0x1000,i==3?sizeof(callee):sizeof(caller)-(pcs[i]-0x1000),
                    pcs[i],nullptr,nullptr,true,false,true,true,i==3?nullptr:&resolve,true,arm_ir_policy::write_spans);
                if(!i && tr.stack_calls!=v)return -1;
                functions.push_back(std::move(tr.func));
            }
            stage_aot_module(build_wasm_module(functions,imports),"tile-call-benchmark");instantiate_staged_modules();
            for(unsigned i=0;i<4;++i)if(!(entries[v][i]=global_registry().lookup(pcs[i])))return -2;
        }
        for(unsigned p=0x10000;p<0x10020;p+=4)memory.write32(p,0x12345678);
        for(unsigned n=0;n<16;++n)memory.write16(0x30000+n*2,0x2000+n);
        std::vector<memory_experiment::binding> bindings;
        for(unsigned page=0x10000;page<test_mem::SIZE;page+=4096)
            bindings.push_back({page,static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(memory.data.data()+page)),3});
        pointer=view.enter(1,1,[&]{return bindings;});ready=true;
    }
    if(variant>1||!iterations)return -3;
    for(unsigned i=0;i<4;++i)global_registry().register_function(pcs[i],entries[variant][i]);
    auto *cpu=matched_kernel_access::state(*core);cpu->Reset();for(auto &reg:cpu->Reg)reg=0;
    cpu->NFlag=cpu->ZFlag=cpu->CFlag=cpu->VFlag=0;
    cpu->Reg[4]=0x80000;cpu->Reg[5]=0x10000;cpu->Reg[6]=0x30000;cpu->Reg[7]=0x2000;
    cpu->Reg[9]=iterations;cpu->Reg[13]=0xf0000;cpu->Reg[14]=0x5000;cpu->Reg[15]=0x1000;
    cpu->Cpsr=16;cpu->TFlag=0;cpu->NirqSig=1;cpu->NumInstrsToExecute=UINT32_MAX;cpu->aot_tlb=pointer;
    watchdog::request=0;g_test_mem=&memory;g_all_memory_helper_calls=0;
    const auto progress=execute_chain(cpu,entries[variant][0]).progress;g_test_mem=nullptr;
    return progress==1 && !cpu->Reg[9] && cpu->Reg[13]==0xf0000 && cpu->Reg[15]==0x5000 && !g_all_memory_helper_calls
        && memory.read32(0x80000)==0x20072008 && memory.read32(0x80000+7*512+12)==0x20012002 ? 0 : -4;
}

#include "tile_call_image.inc"
static int tile_rows(unsigned variant, unsigned iterations, unsigned blend) {
    if(!eka2l1::log::filterings) {
        eka2l1::log::filterings=std::make_unique<eka2l1::log_filterings>();
        eka2l1::log::filterings->reset_all(spdlog::level::off);
    }
    static test_mem memory;
    static std::vector<std::uint32_t> arena(memory_experiment::direct_size/4);
    static r12l1::exclusive_monitor monitor(1);
    static auto core=make_cpu(memory,monitor);
    static memory_experiment::view view;
    static aot_func entries[2][5]{};
    static std::uintptr_t pointer=0;
    static bool ready=false;
    const unsigned pcs[]={0x70020df8,0x70020e8c,0x700032bc,0x70003bc4,0x70003cb4};
    const unsigned callee_sizes[]={61*4,60*4,54*4};
    auto word=[&](unsigned address)->unsigned&{return arena[(address-0x400000)/4];};
    if(!ready) {
        hotpath_policy=2;memory_experiment::mode=2;
        eka2l1::common::code_tracking::unsafe_code_mode=3;
        global_registry().configure_rom_index(0x70000000,0x30000,true);
        const auto cb=reinterpret_cast<const std::uint8_t*>(tile_caller);
        const auto fb=reinterpret_cast<const std::uint8_t*>(tile_functions);
        // Relocate the literal function table exactly as the loader does.
        for(unsigned address=0x7002102c;address<0x70021088;address+=4) {
            unsigned value;std::memcpy(&value,cb+address-0x70020df8,4);
            memory.write32(0x50000+(address&4095),value+0x6fff8000);
        }
        leaf_resolver resolve=[&](unsigned pc) {
            if(pc>=0x7002102c && pc<0x70021088) {
                const auto *p=memory.data.data()+0x50000+(pc&4095);
                return std::vector<std::uint8_t>(p,p+4);
            }
            if(pc>=0x70003290 && pc<0x70004000) {
                const auto n=std::min(512u,0x70004000-pc),offset=pc-0x70003290;
                return std::vector<std::uint8_t>(fb+offset,fb+offset+n);
            }
            return std::vector<std::uint8_t>{};
        };
        const std::vector<wasm_import_func> imports{{"env","tlb_read32",2,true},{"env","tlb_write32",3,false},
            {"env","tlb_read8",2,true},{"env","tlb_write8",3,false},{"env","tlb_read16",2,true},{"env","tlb_write16",3,false}};
        for(unsigned v=0;v<2;++v) {
            arm_indirect_calls=v;std::vector<wasm_func_def> functions;
            for(unsigned n=0;n<5;++n) {
                const auto bytes=n>=2?fb+pcs[n]-0x70003290:cb+pcs[n]-0x70020df8;
                auto tr=translate_arm_block(bytes,n>=2?callee_sizes[n-2]:std::min<std::size_t>(512,sizeof(tile_caller)-(pcs[n]-0x70020df8)),pcs[n],nullptr,nullptr,true,false,true,true,
                    n>=2?nullptr:&resolve,true,arm_ir_policy::write_spans);
                if(!n && (v?!tr.stack_calls:tr.stack_calls))return -1;
                if(!n)printf(" ROW_VARIANT %u BYTES %zu CALLEES %u\n",v,tr.func.body.size(),tr.stack_calls);
                functions.push_back(std::move(tr.func));
            }
            stage_aot_module(build_wasm_module(functions,imports),"tile-row-benchmark");instantiate_staged_modules();
            for(unsigned n=0;n<5;++n)if(!(entries[v][n]=global_registry().lookup(pcs[n])))return -2;
        }
        for(unsigned n=0;n<8;++n)word(0x420020+n*4)=0x12345678;
        for(unsigned n=0;n<8;++n)word(0x440000+n*4)=(0x2001+n*2)<<16 | (0x2000+n*2);
        for(unsigned n=0;n<8;++n)word(0x414000+n*4)=0x00010001;
        word(0x410004)=0x412000;word(0x411014)=0x440000;
        word(0x41101c)=0x420000;word(0x411028)=0x430000;
        word(0x4120f4)=32;word(0x460048)=1;word(0x460078)=0x460000;word(0x46007c)=0x5000;
        std::vector<memory_experiment::binding> bindings{{0x70021000,static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(memory.data.data()+0x50000)),3}};
        pointer=view.enter(1,1,[&]{return bindings;},{0x400000,memory_experiment::direct_size,
            static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(arena.data())),3});ready=true;
    }
    if(variant>1||!iterations)return -3;
    word(0x410008)=blend<<8;word(0x430000)=blend?0x8000:0;
    for(unsigned row=0;row<8;++row)for(unsigned pixel=0;pixel<64;++pixel)
        word(0x450000+row*512+pixel*4)=blend==5?0xffffffffu:0;
    for(unsigned n=0;n<5;++n)global_registry().register_function(pcs[n],entries[variant][n]);
    auto *cpu=matched_kernel_access::state(*core);g_test_mem=&memory;g_all_memory_helper_calls=0;
    for(unsigned repeat=0;repeat<iterations;++repeat) {
        cpu->Reset();for(auto &reg:cpu->Reg)reg=0;
        cpu->NFlag=cpu->ZFlag=cpu->CFlag=cpu->VFlag=0;
        cpu->Reg[4]=0x410000;cpu->Reg[6]=0x411000;cpu->Reg[8]=0x414000;
        cpu->Reg[9]=0x450000;cpu->Reg[10]=16;cpu->Reg[11]=0x460080;cpu->Reg[12]=0x412000;cpu->Reg[13]=0x460000;cpu->Reg[15]=pcs[0];
        cpu->Cpsr=16;cpu->TFlag=0;cpu->NirqSig=1;cpu->NumInstrsToExecute=UINT32_MAX;cpu->aot_tlb=pointer;
        watchdog::request=0;word(0x460050)=0;
        const auto progress=execute_chain(cpu,entries[variant][0]).progress;
        if(progress!=1 || cpu->Reg[15]!=0x5000 || cpu->Reg[13]!=0x460000 || g_all_memory_helper_calls)return -4;
    }
    g_test_mem=nullptr;
    for(unsigned row=0;row<8;++row)for(unsigned tile=0;tile<16;++tile)for(unsigned pair=0;pair<4;++pair) {
        const unsigned lo=8-pair*2,hi=lo-1;
        auto expected=[&](unsigned nibble) {
            if(!blend)return 0x2000+nibble;
            unsigned pixel=0;
            for(unsigned shift:{0u,6u,11u}) {
                const auto amount=std::min<std::uint64_t>(31,std::uint64_t(((0x2000+nibble)>>shift)&31)*iterations);
                pixel|=unsigned(blend==5?31-amount:amount)<<shift;
            }
            return pixel;
        };
        if(word(0x450000+row*512+tile*16+pair*4)!=(expected(hi)<<16 | expected(lo)))return -5;
    }
    return 0;
}

extern "C" EMSCRIPTEN_KEEPALIVE int tile_row_benchmark(unsigned variant, unsigned iterations) {
    return tile_rows(variant,iterations,0);
}
extern "C" EMSCRIPTEN_KEEPALIVE int tile_add_row_benchmark(unsigned variant, unsigned iterations) {
    return tile_rows(variant,iterations,4);
}
extern "C" EMSCRIPTEN_KEEPALIVE int tile_sub_row_benchmark(unsigned variant, unsigned iterations) {
    return tile_rows(variant,iterations,5);
}
#endif
