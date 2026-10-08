// The reference path executes the existing generated instructions separately.
// Runtime cases also pass through the real pending-SVC handler and CPU loop.
static bool test_svc_return_hint() {
#ifdef __EMSCRIPTEN__
    struct restore {
        unsigned memory=memory_experiment::mode;
        bool thumb=thumb_direct_memory,svc=compiled_svc_enabled;
        ~restore(){memory_experiment::mode=memory;thumb_direct_memory=thumb;compiled_svc_enabled=svc;}
    } saved;
    thumb_direct_memory=true;compiled_svc_enabled=true;
    unsigned checks=0;
    for(unsigned mode:{0u,2u})for(unsigned reg=0;reg<10;++reg)for(unsigned shape=0;shape<6;++shape)
    for(unsigned budget:{0u,1u,2u,3u,4u}) {
        memory_experiment::mode=mode;
        const unsigned base=0x51000,target=shape==5?0x51ffc:0x52000,delta=target-base-4;
        std::vector<std::uint8_t> code(0x1008);
        auto put16=[&](unsigned pc,unsigned v){std::uint16_t n=v;std::memcpy(code.data()+pc-base,&n,2);};
        auto put32=[&](unsigned pc,unsigned v){std::memcpy(code.data()+pc-base,&v,4);};
        put16(base,0xf000|((delta>>12)&0x7ff));put16(base+2,0xe800|((delta>>1)&0x7ff));
        put16(base+4,shape==3?0x4770:0xbd00|(reg<8?1u<<reg:reg==8?0:3));
        put32(target,0xef000005);put32(target+4,shape==2?0x112fff1e:0xe12fff1e);
        const code_window rom{code.data(),base,shape==1?target-base+4:static_cast<unsigned>(code.size())};
        auto tr=translate_thumb_block(code.data(),6,base,nullptr,nullptr,true,false,true,shape==4?nullptr:&rom);
        const auto module=build_wasm_module({tr.func},{});
        alignas(8)std::uint32_t state[256]{};
        state[15]=base;state[state_offsets::TFLAG/4]=1;state[state_offsets::CPSR/4]=48;
        state[state_offsets::NIRQ/4]=1;state[state_offsets::NUM_INSTRS_TO_EXECUTE/4]=100;
        state[state_offsets::AOT_BUDGET/4]=budget;
        const auto count=js_run_aot_wasm(module.data(),module.size(),reinterpret_cast<std::uint8_t*>(state),sizeof(state));
        const auto hint=state[state_offsets::AOT_EXIT/4]&0x1f000000u;
        const auto expected=mode==2 && reg<9 && shape==0 && budget>=3 ? svc_return|(reg<<svc_return_register_shift) : 0;
        if(count<0 || hint!=expected) {
            printf(" FAIL SVC return hint mode=%u reg=%u shape=%u budget=%u got=%x expected=%x\n",mode,reg,shape,budget,hint,expected);return false;
        }
        ++checks;
    }
    printf(" PASS SVC return hint (%u immutable/pattern/backend/budget comparisons)\n",checks);
#endif
    return true;
}

static bool test_svc_return_state() {
#ifdef __EMSCRIPTEN__
    namespace memory=memory_experiment;
    struct restore {
        unsigned mode=memory::mode,budget=entry_budget_mode;
        bool thumb=thumb_direct_memory;
        ~restore(){memory::mode=mode;entry_budget_mode=budget;thumb_direct_memory=thumb;}
    } saved;
    memory::mode=2;thumb_direct_memory=true;entry_budget_mode=2;
    test_mem backing;r12l1::exclusive_monitor monitor(1);auto core=make_cpu(backing,monitor);
    auto cpu=std::make_unique<ARMul_State>(core.get(),USER32MODE);
    std::vector<memory::page> pages(1<<20);
    std::vector<std::uint8_t> arena(memory::direct_size);
    const auto arena_host=static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(arena.data()));
    memory::direct_view view{memory::direct_begin,memory::direct_size,arena_host,
        static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(pages.data())),arena_host-memory::direct_begin,0};
    const std::uint32_t bx=0xe12fff1e;
    auto arm=translate_arm_block(reinterpret_cast<const std::uint8_t*>(&bx),4,0x2004,nullptr,nullptr,true,false,true);
    const auto arm_module=build_wasm_module({arm.func},{});
    const std::vector<wasm_import_func> imports={{"env","tlb_read32",2,true},{"env","tlb_write32",3,false},
        {"env","tlb_read8",2,true},{"env","tlb_write8",3,false},{"env","tlb_read16",2,true},{"env","tlb_write16",3,false}};
    unsigned checks=0,full=0,partial=0,declined=0;
    for(unsigned reg=0;reg<9;++reg) {
        const std::uint16_t pop=0xbd00|(reg<8?1u<<reg:0);
        auto thumb=translate_thumb_block(reinterpret_cast<const std::uint8_t*>(&pop),2,0x1004,nullptr,nullptr,true,false,true);
        const auto thumb_module=build_wasm_module({thumb.func},imports);
        for(unsigned mapping=0;mapping<13;++mapping)for(unsigned budget:{0u,1u,2u,5u})
        for(unsigned irq=0;irq<4;++irq)for(unsigned change=0;change<4;++change)for(unsigned thumb_pc:{0u,1u}) {
            const bool affine=mapping>=9;
            const unsigned sp=mapping==1?0x8011:mapping==2?0x8ffc:mapping==3?0x8ff9:
                mapping==7?0xfffe0010:mapping==8?0xfffffffc:
                mapping==9?memory::direct_begin+4092:mapping==10?memory::direct_begin+memory::direct_size-8:
                mapping==11?memory::direct_begin+memory::direct_size-4:mapping==12?memory::direct_begin+17:0x8010;
            pages[sp>>12]={};
            view.arena_mask=affine?~0u:0;
            auto *page=backing.data.data()+(mapping==6?0x9000:0x8000);
            if(!affine && mapping!=4)pages[sp>>12].read=static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(page));
            const unsigned bytes=reg<8?8:4;
            const bool span=affine ? std::uint64_t(sp-memory::direct_begin)+bytes<=memory::direct_size : (sp&4095)+bytes<=4096;
            const bool readable=span && mapping!=4 && mapping!=5;
            if(span) {
                auto *host=affine?arena.data()+sp-memory::direct_begin:page+(sp&4095);
                const unsigned value=0xaabbddeeu,target=0x5000|thumb_pc;
                if(reg<8)std::memcpy(host,&value,4);
                std::memcpy(host+(reg<8?4:0),&target,4);
            }
            cpu->Reset();for(unsigned n=0;n<16;++n)cpu->Reg[n]=0x77665500+n;
            cpu->Reg[13]=sp;cpu->Reg[14]=change==1?0x1009:0x1005;cpu->Reg[15]=change==2?0x3004:0x2004;
            cpu->Cpsr=16|(irq==2?0x80:0)|(mapping==5?0x200:0)|0xa0000000;
            cpu->NFlag=1;cpu->ZFlag=0;cpu->CFlag=1;cpu->VFlag=0;cpu->TFlag=change==3;
            cpu->NirqSig=irq==0?1:0;cpu->NumInstrsToExecute=irq==3?0:std::uint64_t(1)<<32;
            cpu->aot_budget=budget;cpu->aot_tlb=reinterpret_cast<std::uintptr_t>(&view);cpu->aot_exit=0;
            alignas(8)std::uint32_t expected[256]{};
            constexpr unsigned state_bytes=state_offsets::AOT_SVC_INSTRUCTIONS+4;
            std::memcpy(expected,cpu.get(),state_bytes);
            const unsigned count=complete_svc_return(cpu.get(),mapping==4?nullptr:&view,
                svc_return|(reg<<svc_return_register_shift),0x2004,0x1005,budget);
            const bool outgoing=irq==0||irq==2;
            const unsigned want=change||!budget?0:budget>1&&outgoing&&readable?2:1;
            if(count!=want) {
                printf(" FAIL SVC return progress reg=%u mapping=%u budget=%u irq=%u change=%u got=%u expected=%u\n",reg,mapping,budget,irq,change,count,want);return false;
            }
            if(count) {
                expected[state_offsets::AOT_BUDGET/4]=budget;
                auto n=js_run_aot_wasm(arm_module.data(),arm_module.size(),reinterpret_cast<std::uint8_t*>(expected),sizeof(expected));
                if(n!=1)return false;
                if(budget>1 && outgoing)expected[15]&=~1u;
                if(count==2) {
                    expected[state_offsets::AOT_BUDGET/4]=budget-1;g_all_memory_helper_calls=0;
                    n=js_run_aot_wasm(thumb_module.data(),thumb_module.size(),reinterpret_cast<std::uint8_t*>(expected),sizeof(expected));
                    if(n!=1||g_all_memory_helper_calls)return false;
                }
                expected[state_offsets::AOT_BUDGET/4]=budget;
            }
            if(std::memcmp(expected,cpu.get(),state_bytes)) {
                printf(" FAIL SVC return state reg=%u mapping=%u budget=%u irq=%u change=%u count=%u\n",reg,mapping,budget,irq,change,count);
                const auto *actual=reinterpret_cast<const unsigned*>(cpu.get());
                for(unsigned n=0;n<state_bytes/4;++n)if(expected[n]!=actual[n])printf(" state[%u]=%x/%x\n",n,actual[n],expected[n]);
                return false;
            }
            ++checks;if(count==2)++full;else if(count)++partial;else ++declined;
        }
    }
    printf(" PASS SVC return state (%u full-state comparisons: %u complete, %u partial, %u declined)\n",checks,full,partial,declined);
#endif
    return true;
}

static bool test_svc_return_runtime() {
#ifdef __EMSCRIPTEN__
    struct restore {
        unsigned memory=memory_experiment::mode;
        bool thumb=thumb_direct_memory,svc=compiled_svc_enabled,ram=ram_compilation_enabled,hot=hot_compilation_enabled,chain=chaining_enabled;
        ~restore(){memory_experiment::mode=memory;thumb_direct_memory=thumb;compiled_svc_enabled=svc;
            ram_compilation_enabled=ram;hot_compilation_enabled=hot;chaining_enabled=chain;
            for(unsigned pc:{0x51001u,0x51005u,0x51009u,0x52004u,0x52008u})global_registry().unregister_function(pc);}
    } saved;
    memory_experiment::mode=2;thumb_direct_memory=true;compiled_svc_enabled=true;
    ram_compilation_enabled=false;hot_compilation_enabled=false;
    test_mem rom;
    const unsigned base=0x51000,target=0x52000,delta=target-base-4;
    rom.write16(base,0xf000|((delta>>12)&0x7ff));rom.write16(base+2,0xe800|((delta>>1)&0x7ff));
    rom.write16(base+4,0xbd10);rom.write16(base+8,0xbd20);
    rom.write32(target,0xef000005);rom.write32(target+4,0xe12fff1e);rom.write32(target+8,0xe12fff1e);
    rom.write16(0x53000,0x3001);rom.write16(0x53002,0xe7fd);
    rom.write32(0x53100,0xe2800001);rom.write32(0x53104,0xeafffffd);
    const std::vector<wasm_import_func> imports={{"env","tlb_read32",2,true},{"env","tlb_write32",3,false},
        {"env","tlb_read8",2,true},{"env","tlb_write8",3,false},{"env","tlb_read16",2,true},{"env","tlb_write16",3,false}};
    std::vector<wasm_func_def> returns;
    for(unsigned pc:{base+4,base+8}) {
        auto tr=translate_thumb_block(rom.data.data()+pc,2,pc,nullptr,nullptr,true,false,true);
        tr.func.export_name="f_"+std::to_string(pc|1);returns.push_back(std::move(tr.func));
    }
    for(unsigned pc:{target+4,target+8})returns.push_back(translate_arm_block(rom.data.data()+pc,4,pc,nullptr,nullptr,true,false,true).func);
    stage_aot_module(build_wasm_module(returns,imports),"test-svc-return");instantiate_staged_modules();
    aot_func callers[2]{};
    for(unsigned variant=0;variant<2;++variant) {
        const code_window immutable{rom.data.data()+base,base,target-base+(variant?8u:4u)};
        auto tr=translate_thumb_block(rom.data.data()+base,6,base,nullptr,nullptr,true,false,true,&immutable);
        tr.func.export_name="f_"+std::to_string(base|1);
        stage_aot_module(build_wasm_module({tr.func},imports),"test-svc-return");instantiate_staged_modules();
        callers[variant]=global_registry().lookup(base|1);
        if(!callers[variant])return false;
    }
    std::vector<memory_experiment::page> pages(1<<20);
    unsigned checks=0;
    for(bool chain:{false,true})for(unsigned action=0;action<12;++action)
    for(unsigned budget:{1u,2u,3u,4u,5u,6u,7u,8u,16u,32u})for(bool thumb_pc:{false,true}) {
        chaining_enabled=chain;
        std::vector<std::uint32_t> outcomes[2],events[2];
        for(unsigned variant=0;variant<2;++variant) {
            test_mem memory;memory.data=rom.data;r12l1::exclusive_monitor monitor(1);auto core=make_cpu(memory,monitor);
            auto *cpu=matched_kernel_access::state(*core);
            const unsigned sp=action==9?0x8ffcu:0x8010u;
            const unsigned return_pc=thumb_pc?0x53001u:0x53100u;
            memory.write32(sp,0x33445566);memory.write32(sp+4,return_pc);
            memory.write32(0xa010,0x778899aa);memory.write32(0xa014,return_pc);
            bool alias=false,repaired=false;
            auto publish=[&] {
                pages[8]={static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(memory.data.data()+(alias?0xa000:0x8000))),0};
                pages[9]={};
                if(action==8)pages[8]={};
            };
            publish();
            memory_experiment::direct_view view{0,0,0,static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(pages.data())),0,0};
            core->experimental_memory=[&](bool enter)->std::uintptr_t{if(enter)publish();return reinterpret_cast<std::uintptr_t>(&view);};
            core->experimental_pointer=reinterpret_cast<std::uintptr_t>(&view);
            core->experimental_dirty=std::make_shared<std::atomic<bool>>(false);
            auto snapshot=[&](unsigned event,unsigned argument) {
                auto &log=events[variant];log.push_back(event);log.push_back(argument);
                for(unsigned reg=0;reg<16;++reg)log.push_back(cpu->Reg[reg]);
                log.push_back(core->get_cpsr());log.push_back(cpu->NumInstrsToExecute);log.push_back(cpu->NumInstrsToExecute>>32);
            };
            core->read_32bit=[&](unsigned address,unsigned *out) {
                snapshot(2,address);
                if(action==9 && address==sp+4 && !repaired)return false;
                if(address>test_mem::SIZE-4)return false;
                *out=memory.read32(alias && (address>>12)==8 ? address+0x2000:address);return true;
            };
            core->exception_handler=[&](auto type,unsigned address) {
                snapshot(3,address);repaired=true;return true;
            };
            core->system_call_handler=[&](unsigned number) {
                snapshot(1,number);
                cpu->Reg[0]=0x1234;core->set_cpsr(core->get_cpsr()^0xa0000000);
                if(action==1)cpu->Reg[14]=base+9;
                if(action==2)cpu->Reg[15]=target+8;
                if(action==3)core->set_cpsr(core->get_cpsr()|32);
                if(action==4||action==10)cpu->NirqSig=0;
                if(action==5)core->stop();
                if(action==6)core->set_cpsr(core->get_cpsr()|0x200);
                if(action==7){alias=true;core->experimental_dirty->store(true);}
                if(action==11){cpu->Reg[13]+=4;memory.write32(sp+4,0xabcddcba);memory.write32(sp+8,return_pc);}
            };
            for(unsigned reg=0;reg<16;++reg)core->set_reg(reg,0x88776600+reg);
            core->set_pc(base);core->set_reg(13,sp);core->set_cpsr(0x50000030|(action==10?0x80:0));cpu->NirqSig=1;
            global_registry().register_function(base|1,callers[variant]);
            core->run(budget);
            auto &result=outcomes[variant];for(unsigned reg=0;reg<16;++reg)result.push_back(core->get_reg(reg));
            result.push_back(core->get_cpsr());result.push_back(core->get_num_instruction_executed());
            result.push_back(cpu->NirqSig);result.push_back(cpu->NumInstrsToExecute);
        }
        if(outcomes[0]!=outcomes[1] || events[0]!=events[1]) {
            printf(" FAIL real SVC return chain=%u action=%u budget=%u thumb=%u events=%zu/%zu\n",chain,action,budget,thumb_pc,events[0].size(),events[1].size());
            for(unsigned n=0;n<outcomes[0].size();++n)if(outcomes[0][n]!=outcomes[1][n])printf(" result[%u]=%x/%x\n",n,outcomes[0][n],outcomes[1][n]);
            for(unsigned n=0;n<std::min(events[0].size(),events[1].size());++n)if(events[0][n]!=events[1][n])printf(" event[%u]=%x/%x\n",n,events[0][n],events[1][n]);
            return false;
        }
        ++checks;
    }
    printf(" PASS real SVC return loop (%u state/count/callback/IRQ/stop/remap/fault comparisons)\n",checks);
#endif
    return true;
}
