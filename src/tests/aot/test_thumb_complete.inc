// Compare whole compiled chains, including observable callback boundaries.
static bool test_thumb_complete_calls(bool instrument = false) {
#ifdef __EMSCRIPTEN__
    using S = state_offsets;
    struct restore {
        bool direct=thumb_direct_memory, fusion=thumb_region_fusion, complete=thumb_complete_calls;
        unsigned mode=memory_experiment::mode, unsafe=eka2l1::common::code_tracking::unsafe_code_mode;
        ~restore(){thumb_direct_memory=direct;thumb_region_fusion=fusion;thumb_complete_calls=complete;
            memory_experiment::mode=mode;eka2l1::common::code_tracking::unsafe_code_mode=unsafe;
            g_test_mem=nullptr;g_memory_boundary={};}
    } saved;
    thumb_direct_memory=true;thumb_region_fusion=true;eka2l1::common::code_tracking::unsafe_code_mode=3;
    std::vector<wasm_import_func> imports={{"env","tlb_read32",2,true},{"env","tlb_write32",3,false},
        {"env","tlb_read8",2,true},{"env","tlb_write8",3,false},{"env","tlb_read16",2,true},{"env","tlb_write16",3,false}};
    if(instrument)imports.push_back({"env","eka_test_count",1,false});
    std::vector<std::vector<std::uint16_t>> leaves={
        {0x6800,0x4770},
        {0x0001,0x68c9,0x2001,0x2900,0xd100,0x2000,0x4770},
        {0x7840,0x4770}, {0x8840,0x4770}, {0x9801,0x4770},
        {0x4670,0x4770}, {0x3001,0x4148,0x4188,0x4770},
        {0x4800,0x4770,0x8001,0x1234},
        {0x6001,0x4770},             // store: retain ordinary fusion
        {0x4686,0x4770},             // LR write: retain dynamic return
        {0x3801,0xd1fd,0x4770},      // cycle: do not select complete fusion
    };
    for(unsigned cond=0;cond<14;++cond)
        leaves.push_back({static_cast<std::uint16_t>(0xd000|(cond<<8)|1),0x2401,0x4770,0x2402,0x4770});
    std::vector<std::vector<std::uint16_t>> programs;
    for(const auto &leaf:leaves) {
        std::vector<std::uint16_t> words={0x46f0,0xf000,0xf80d,0x3601,0x4740};
        words.resize(16,0x46c0);words.insert(words.end(),leaf.begin(),leaf.end());programs.push_back(std::move(words));
    }
    unsigned checks=0,independent=0,selected=0,less=0,equal=0,more=0;std::uint64_t calls[2]{};
    double operations[2][12]{};
    for(unsigned policy:{0u,2u})for(unsigned program=0;program<programs.size();++program) {
        memory_experiment::mode=policy;
        const auto &words=programs[program];
        const code_window rom{reinterpret_cast<const std::uint8_t*>(words.data()),0x1000,static_cast<unsigned>(words.size()*2)};
        std::map<unsigned,std::vector<std::uint8_t>> modules[2];
        for(unsigned variant=0;variant<2;++variant)for(unsigned off=0;off<rom.size;off+=2) {
            thumb_complete_calls=variant;
            auto tr=translate_thumb_block(rom.host+off,rom.size-off,rom.base+off,nullptr,nullptr,true,false,true,&rom);
            selected+=tr.inlined_thumb_helpers;
            if(instrument && !count_fusion_operations(tr.func))return false;
            modules[variant][rom.base+off]=build_wasm_module({tr.func},imports);
        }
        for(unsigned input:{0u,1u,5u})for(unsigned flags:{0u,5u,10u,15u})
        for(unsigned budget:{1000u})for(unsigned mapping:{0u,1u})for(unsigned event:{0u,1u,2u,3u}) {
            if(instrument && (event || flags==5 || flags==10))continue;
            double path_cost[2]{};unsigned path_calls[2]{};
            test_mem memories[2];memories[0].write32(0x8000,input);memories[0].write32(0x8004,input+1);memories[0].write32(0x800c,input);memories[0].write_code(rom.base,{rom.host,rom.host+rom.size});memories[1]=memories[0];
            alignas(8) unsigned states[2][256]{};unsigned counts[2]{},helpers[2]{};
            std::vector<std::array<unsigned,22>> observations[2];
            for(unsigned variant=0;variant<2;++variant) {
                if(instrument)for(unsigned kind=0;kind<12;++kind)fusion_operation_count(kind,true);
                auto *state=states[variant];
                for(unsigned reg=0;reg<15;++reg)state[reg]=0x12345600+reg;
                state[0]=program==10 ? 2 : 0x8000;state[1]=0x8000;state[3]=input;state[13]=0x9000;state[14]=0x3001;state[15]=rom.base;
                state[S::CPSR/4]=0x30|(flags<<28);state[S::TFLAG/4]=1;
                state[S::NFLAG/4]=(flags>>3)&1;state[S::ZFLAG/4]=(flags>>2)&1;state[S::CFLAG/4]=(flags>>1)&1;state[S::VFLAG/4]=flags&1;
                state[S::NIRQ/4]=1;state[S::NUM_INSTRS_TO_EXECUTE/4]=100;
                r12l1::tlb tlb(12);if(mapping)tlb.add(0x8000,memories[variant].data.data()+0x8000,3);
                memory_experiment::view view;
                const auto pointer=view.enter(1,1,[&]{return std::vector<memory_experiment::binding>{{0x8000,
                    static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(memories[variant].data.data()+0x8000)),mapping?3u:0u}};});
                state[S::AOT_TLB/4]=policy?static_cast<unsigned>(pointer):reinterpret_cast<std::uintptr_t>(tlb.entries);
                g_test_mem=&memories[variant];g_all_memory_helper_calls=0;
                g_memory_boundary=[&](bool after,unsigned ptr) {
                    auto *s=reinterpret_cast<unsigned*>(ptr);
                    if(after && event==1)s[S::NUM_INSTRS_TO_EXECUTE/4]=0;
                    if(after && event==2)s[S::NIRQ/4]=0;
                    if(after && event==3){s[14]=0x3001;s[3]=7;s[S::CFLAG/4]=1;s[S::ZFLAG/4]=0;}
                    std::array<unsigned,22> observation{};std::copy(s,s+16,observation.begin());
                    for(unsigned i=0;i<6;++i)observation[16+i]=s[std::array<unsigned,6>{S::NFLAG,S::ZFLAG,S::CFLAG,S::VFLAG,S::TFLAG,S::CPSR}[i]/4];
                    observations[variant].push_back(observation);
                };
                for(unsigned round=0;round<64 && counts[variant]<budget;++round) {
                    auto it=modules[variant].find(state[15]);
                    if(it==modules[variant].end() || !state[S::TFLAG/4] || !state[S::NUM_INSTRS_TO_EXECUTE/4]
                            || (!state[S::NIRQ/4] && !(state[S::CPSR/4]&0x80)))break;

                    const auto n=js_run_aot_wasm(it->second.data(),it->second.size(),reinterpret_cast<std::uint8_t*>(state),sizeof(states[variant]));
                    ++calls[variant];++path_calls[variant];if(n<0)return false;if(!n)break;counts[variant]+=n;
                    if(counts[variant]>budget)return false;
                }
                if(instrument)for(unsigned kind=0;kind<12;++kind) {
                    const auto n=fusion_operation_count(kind,false);operations[variant][kind]+=n;path_cost[variant]+=n;
                }
                helpers[variant]=g_all_memory_helper_calls;state[S::AOT_TLB/4]=0;
            }
            if(std::memcmp(states[0],states[1],sizeof(states[0]))
                    || helpers[0]!=helpers[1] || memories[0].data!=memories[1].data || observations[0]!=observations[1]) {
                printf(" FAIL fusion policy=%u program=%u input=%u flags=%u budget=%u mapping=%u event=%u counts=%u/%u helpers=%u/%u\n",policy,program,input,flags,budget,mapping,event,counts[0],counts[1],helpers[0],helpers[1]);
                for(unsigned i=0;i<256;++i)if(states[0][i]!=states[1][i])printf(" state[%u] %x/%x\n",i,states[0][i],states[1][i]);return false;
            }
            if(!event && mapping && counts[1]) {
                test_mem reference;reference.write32(0x8000,input);reference.write32(0x8004,input+1);reference.write32(0x800c,input);
                reference.write_code(rom.base,std::vector<std::uint8_t>(rom.host,rom.host+rom.size));
                r12l1::exclusive_monitor mon(1);auto cpu=make_cpu(reference,mon);
                for(unsigned reg=0;reg<15;++reg)cpu->set_reg(reg,0x12345600+reg);
                cpu->set_reg(0,program==10 ? 2 : 0x8000);cpu->set_reg(1,0x8000);cpu->set_reg(3,input);cpu->set_reg(13,0x9000);cpu->set_reg(14,0x3001);
                cpu->set_pc(rom.base);cpu->set_cpsr(0x30|(flags<<28));reference_to(*cpu,states[1]);
                for(unsigned reg=0;reg<16;++reg)if(cpu->get_reg(reg)!=(reg==15 ? states[1][reg] & (states[1][S::TFLAG/4]?~1u:~3u) : states[1][reg])) {
                    printf(" FAIL fusion interpreter program=%u budget=%u reg=%u %x/%x\n",program,budget,reg,cpu->get_reg(reg),states[1][reg]);return false;
                }
                const auto actual=capture_state(*cpu);
                if(actual.nflag!=states[1][S::NFLAG/4]||actual.zflag!=states[1][S::ZFLAG/4]
                    ||actual.cflag!=states[1][S::CFLAG/4]||actual.vflag!=states[1][S::VFLAG/4]
                    ||reference.read32(0x8000)!=memories[1].read32(0x8000))return false;
                ++independent;
            }
            if(instrument){
                less+=path_cost[1]<path_cost[0];equal+=path_cost[1]==path_cost[0];more+=path_cost[1]>path_cost[0];
                if(path_cost[1]>path_cost[0] && more<=8)printf(" GROWTH program=%u budget=%u input=%u mapping=%u cost=%.0f/%.0f calls=%u/%u\n",program,budget,input,mapping,path_cost[0],path_cost[1],path_calls[0],path_calls[1]);
            }
            ++checks;
        }
    }
    if(instrument) {
        printf(" FUSION operations: paths lower=%u equal=%u higher=%u\n",less,equal,more);
        for(unsigned kind=0;kind<12;++kind)printf(" category %u %.0f -> %.0f\n",kind,operations[0][kind],operations[1][kind]);
    }
    if(!selected || calls[1]>=calls[0])return false;
    printf(" PASS %s (%u chain/state/memory/callback checks; %u independent interpreter checks; calls %llu -> %llu; %u selected sites)\n","complete Thumb calls",checks,independent,static_cast<unsigned long long>(calls[0]),static_cast<unsigned long long>(calls[1]),selected);
#endif
    return true;
}

#ifdef __EMSCRIPTEN__
// The sampled ROM boolean helper and its caller's MOVS/BL/CMP sequence,
// surrounded by a fixed-work loop. Use the production runner and both policies.
extern "C" EMSCRIPTEN_KEEPALIVE int thumb_complete_benchmark(unsigned variant, unsigned iterations) {
    if (!eka2l1::log::filterings) {
        eka2l1::log::filterings = std::make_unique<eka2l1::log_filterings>();
        eka2l1::log::filterings->reset_all(spdlog::level::off);
    }
    static test_mem memory;
    static r12l1::exclusive_monitor monitor(1);
    static auto core = make_cpu(memory, monitor);
    static memory_experiment::view view;
    static std::map<unsigned, aot_func> entries[2];
    static std::uintptr_t pointer = 0;
    static bool ready = false;
    if (!ready) {
        hotpath_policy = 2; memory_experiment::mode = 2;
        thumb_direct_memory = true; thumb_region_fusion = true;
        eka2l1::common::code_tracking::unsafe_code_mode = 3;
        global_registry().configure_rom_index(0x1000, 0x1000, true);
        std::vector<std::uint16_t> words = {
            0x46f0,0x0020,0xf000,0xf80c,0x2800,0x1836,0x3d01,0xd1f8,0x4740};
        words.resize(16, 0x46c0);
        const std::uint16_t helper[] = {0x0001,0x68c9,0x2001,0x2900,0xd100,0x2000,0x4770};
        words.insert(words.end(), std::begin(helper), std::end(helper));
        const code_window rom{reinterpret_cast<const std::uint8_t *>(words.data()), 0x1000,
            static_cast<unsigned>(words.size() * 2)};
        const std::vector<wasm_import_func> imports = {{"env","tlb_read32",2,true},{"env","tlb_write32",3,false},
            {"env","tlb_read8",2,true},{"env","tlb_write8",3,false},{"env","tlb_read16",2,true},{"env","tlb_write16",3,false}};
        const auto saved = thumb_complete_calls;
        for (unsigned v = 0; v < 2; ++v) {
            thumb_complete_calls = v;
            std::vector<wasm_func_def> functions;
            for (unsigned off = 0; off < rom.size; off += 2) {
                auto tr = translate_thumb_block(rom.host + off, rom.size - off, rom.base + off,
                    nullptr, nullptr, true, false, true, &rom);
                if (!off && bool(tr.inlined_thumb_helpers) != bool(v)) return -1;
                tr.func.export_name = "f_" + std::to_string((rom.base + off) | 1);
                functions.push_back(std::move(tr.func));
            }
            stage_aot_module(build_wasm_module(functions, imports), "thumb-complete-benchmark");
            instantiate_staged_modules();
            for (unsigned off = 0; off < rom.size; off += 2) {
                const auto key = (rom.base + off) | 1;
                if (!(entries[v][key] = global_registry().lookup(key))) return -2;
            }
        }
        thumb_complete_calls = saved;
        memory.write32(0x800c, 7);
        pointer = view.enter(1, 1, [&] { return std::vector<memory_experiment::binding>{{0x8000,
            static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(memory.data.data() + 0x8000)), 3}}; });
        ready = true;
    }
    if (variant > 1 || !iterations) return -3;
    for (const auto &[pc, function] : entries[variant]) global_registry().register_function(pc, function);
    auto *cpu = matched_kernel_access::state(*core);
    cpu->Reset(); for (auto &reg : cpu->Reg) reg = 0;
    cpu->NFlag = cpu->ZFlag = cpu->CFlag = cpu->VFlag = 0;
    cpu->Reg[4] = 0x8000; cpu->Reg[5] = iterations; cpu->Reg[14] = 0x3001; cpu->Reg[15] = 0x1000;
    cpu->Cpsr = 0x30; cpu->TFlag = 1; cpu->NirqSig = 1;
    cpu->NumInstrsToExecute = UINT32_MAX; cpu->aot_tlb = pointer;
    watchdog::request = 0; g_test_mem = &memory; g_all_memory_helper_calls = 0;
    const auto progress = execute_chain(cpu, entries[variant].at(0x1001)).progress;
    g_test_mem = nullptr;
    return progress == 1 && !cpu->Reg[5] && cpu->Reg[6] == iterations && cpu->Reg[15] == 0x3000
        && cpu->TFlag && !g_all_memory_helper_calls ? 0 : -4;
}
#endif
