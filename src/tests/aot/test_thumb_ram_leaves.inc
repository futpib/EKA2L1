// RAM leaves keep their dependency mappings and return exactly before a failed
// access. Compare the whole inlined call, including flags and earlier stores,
// against the independent interpreter.
static bool test_thumb_ram_leaves() {
#ifdef __EMSCRIPTEN__
    using S = state_offsets;
    namespace tracking = eka2l1::common::code_tracking;
    struct restore {
        unsigned mode = memory_experiment::mode, unsafe = tracking::unsafe_code_mode;
        ~restore() { memory_experiment::mode = mode; tracking::unsafe_code_mode = unsafe;
            watchdog::request = 0; g_test_mem = nullptr; }
    } saved;
    memory_experiment::mode = 2; tracking::unsafe_code_mode = 3;
    const std::uint16_t caller[] = {0x46f0,0xf000,0xfffd,0x3601,0x4740};
    std::vector<std::vector<std::uint16_t>> programs = {
        {0x2800,0xd000,0x8008,0x4770},
        {0x1842,0x4041,0x0010,0x4048,0x2320,0x4018,0x1a12,0x4051,0x019b,
            0x4019,0x1a52,0x015b,0x4013,0x4319,0x4308,0x1101,0x1a40,0x4310,0x4770},
        {0x6008,0x3104,0x680a,0x3201,0x600a,0x4770},
        {0x7008,0x784a,0x3201,0x704a,0x4770},
        {0x9000,0x9a00,0x3201,0x9201,0x4770},
        {0x4800,0x4770,0x8001,0x1234},
        {0x4670,0x4770},
        {0xe001,0x4686,0x4770,0x3001,0x4770}
    };
    for (unsigned cond=0;cond<14;++cond)
        programs.push_back({static_cast<std::uint16_t>(0xd000|(cond<<8)|1),
            0x6008,0x4770,0x6048,0x4770});
    unsigned checks = 0;
    for (const auto &code : programs) {
        const auto *data = reinterpret_cast<const std::uint8_t *>(code.data());
        leaf_resolver resolve = [&](unsigned pc) {
            return pc == 0x2000 ? std::vector<std::uint8_t>(data,data+code.size()*2)
                               : std::vector<std::uint8_t>{};
        };
        const auto tr = translate_thumb_block(reinterpret_cast<const std::uint8_t *>(caller),
            sizeof(caller),0x1000,nullptr,nullptr,true,true,true,nullptr,&resolve);
        if (!tr.complete || tr.inlined_thumb_helpers != 1 || tr.dependencies.size() != 1
                || tr.dependencies[0].address != 0x2000
                || tr.dependencies[0].bytes.size() != (code[0] == 0x4800 ? 4 : code.size()*2)
                || tr.end_address != 0x1000 + sizeof(caller)) {
            printf("FAIL RAM leaf selection first=%x inlined=%u deps=%zu end=%x\n",
                code[0],tr.inlined_thumb_helpers,tr.dependencies.size(),tr.end_address); return false;
        }
        const auto bytes = build_wasm_module({tr.func}, {{"env","tlb_read32",2,true},
            {"env","tlb_write32",3,false},{"env","tlb_read8",2,true},
            {"env","tlb_write8",3,false},{"env","tlb_read16",2,true},{"env","tlb_write16",3,false}});
        for (unsigned input : {0u,1u,0x80000000u,0xffffffffu})
        for (unsigned flags : {0u,5u,10u,15u})
        for (unsigned fault=0;fault<6;++fault) {
            test_mem memory;
            for (unsigned a=0x8000;a<0xa000;++a) memory.data[a]=(a*37+19)&255;
            memory.write_code(0x1000,{reinterpret_cast<const std::uint8_t *>(caller),
                reinterpret_cast<const std::uint8_t *>(caller)+sizeof(caller)});
            memory.write_code(0x2000,{data,data+code.size()*2});
            test_mem reference = memory;
            alignas(8) unsigned state[256]{};
            for (unsigned r=0;r<15;++r) state[r]=r*73+11;
            state[0]=input; state[1]=fault==3?0x8ffc:fault==4?0x8001:0x8000;
            state[13]=state[1]; state[14]=0x3001; state[15]=0x1000;
            state[S::MODE/4]=16; state[S::CPSR/4]=0x30|(flags<<28)|(fault==5?0x200:0);
            state[S::TFLAG/4]=1; state[S::NIRQ/4]=1; state[S::NUM_INSTRS_TO_EXECUTE/4]=UINT32_MAX;
            state[S::NFLAG/4]=(flags>>3)&1; state[S::ZFLAG/4]=(flags>>2)&1;
            state[S::CFLAG/4]=(flags>>1)&1; state[S::VFLAG/4]=flags&1;
            r12l1::exclusive_monitor mon(1); auto cpu=make_cpu(reference,mon);
            for (unsigned r=0;r<16;++r) cpu->set_reg(r,state[r]);
            cpu->set_cpsr(state[S::CPSR/4]);
            memory_experiment::view view;
            state[S::AOT_TLB/4]=view.enter(1,1,[&]{return std::vector<memory_experiment::binding>{
                {0x8000,static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(memory.data.data()+0x8000)),
                    fault==1?0u:fault==2?1u:3u},
                {0x2000,static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(memory.data.data()+0x2000)),3u}};});
            g_test_mem=&memory; g_all_memory_helper_calls=0; watchdog::request=1;
            const auto progress=js_run_aot_wasm(bytes.data(),bytes.size(),
                reinterpret_cast<std::uint8_t *>(state),sizeof(state));
            watchdog::request=0;
            if (progress<0 || g_all_memory_helper_calls || (!fault && state[15]!=0x3000)
                    || reference_to(*cpu,state)<0 || memory.data!=reference.data) {
                printf("FAIL RAM leaf first=%x input=%x flags=%u fault=%u pc=%x helpers=%u progress=%d\n",
                    code[0],input,flags,fault,state[15],g_all_memory_helper_calls,progress); return false;
            }
            ++checks;
        }
    }
    // A helper ending at the address-space boundary still owns both halfwords.
    const std::uint16_t high_call[]={0xf7fe,0xfffc};
    leaf_resolver high_resolve=[](unsigned pc) { return pc==0xfffffffcu
        ? std::vector<std::uint8_t>{1,0x20,0x70,0x47} : std::vector<std::uint8_t>{}; };
    const auto high=translate_thumb_block(reinterpret_cast<const std::uint8_t *>(high_call),sizeof(high_call),
        0x1000,nullptr,nullptr,true,true,true,nullptr,&high_resolve);
    if(high.inlined_thumb_helpers!=1 || high.dependencies.size()!=1 || high.dependencies[0].bytes.size()!=4)return false;
    // Reject cycles, LR writes, calls, unsupported memory forms and truncated code.
    for (const std::vector<std::uint16_t> code : {
            std::vector<std::uint16_t>{0x3801,0xd1fd,0x4770}, {0x4686,0x4770},
            {0xf000,0xf800,0x4770}, {0xc101,0x4770}, {0x3001}}) {
        leaf_resolver resolve=[&](unsigned) { const auto *p=reinterpret_cast<const std::uint8_t *>(code.data());
            return std::vector<std::uint8_t>(p,p+code.size()*2); };
        const auto tr=translate_thumb_block(reinterpret_cast<const std::uint8_t *>(caller),sizeof(caller),
            0x1000,nullptr,nullptr,true,true,true,nullptr,&resolve);
        if (tr.inlined_thumb_helpers || !tr.dependencies.empty()) return false;
    }
    for (unsigned mode : {0u,2u}) for (unsigned unsafe : {0u,3u}) {
        if (mode==2 && unsafe==3) continue;
        memory_experiment::mode=mode; tracking::unsafe_code_mode=unsafe;
        leaf_resolver resolve=[](unsigned) { return std::vector<std::uint8_t>{0x08,0x80,0x70,0x47}; };
        const auto tr=translate_thumb_block(reinterpret_cast<const std::uint8_t *>(caller),sizeof(caller),
            0x1000,nullptr,nullptr,true,true,true,nullptr,&resolve);
        if (tr.inlined_thumb_helpers || !tr.dependencies.empty()) return false;
    }
    printf("PASS RAM Thumb leaves: %u interpreter state/flags/memory comparisons\n", checks);
#endif
    return true;
}
