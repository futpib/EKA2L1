// Finite scalar loops keep their state in WASM locals. A failed memory proof
// must return before the access, so a callback cannot invalidate termination.
static bool test_thumb_finite_loops() {
#ifdef __EMSCRIPTEN__
    using S = state_offsets;
    namespace tracking = eka2l1::common::code_tracking;
    struct restore {
        bool direct = thumb_direct_memory;
        unsigned memory = memory_experiment::mode, unsafe = tracking::unsafe_code_mode;
        ~restore() { thumb_direct_memory = direct; memory_experiment::mode = memory;
            tracking::unsafe_code_mode = unsafe; watchdog::request = 0; g_test_mem = nullptr; }
    } saved;
    thumb_direct_memory = true;
    tracking::unsafe_code_mode = 3;
    const std::vector<std::uint16_t> pixel = {
        0x8811,0x1c92,0x00ce,0x7006,0x08ce,0x403e,0x1c40,0x0ac9,0x7006,
        0x1c40,0x00c9,0x7001,0x1c40,0x2100,0x7001,0x1c40,0x1e5b,0xd1ed
    };
    const auto translate = [](const std::vector<std::uint16_t> &code) {
        return translate_thumb_block(reinterpret_cast<const std::uint8_t *>(code.data()),
            code.size()*2,0x1000,nullptr,nullptr,true,true,true);
    };
    const auto module = [](const translate_result &tr) {
        return build_wasm_module({tr.func}, {{"env","tlb_read32",2,true},
            {"env","tlb_write32",3,false},{"env","tlb_read8",2,true},
            {"env","tlb_write8",3,false},{"env","tlb_read16",2,true},
            {"env","tlb_write16",3,false}});
    };
    unsigned checks = 0;
    for (unsigned policy : {0u,2u}) {
        memory_experiment::mode = policy;
        const auto tr = translate(pixel);
        if (tr.proved_terminating_loops != 1 || tr.watchdog_safepoints
                || tr.end_address != 0x1000 + pixel.size()*2) return false;
        const auto bytes = module(tr);
        for (unsigned iterations : {1u,2u,17u,113u})
        for (unsigned flags : {0u,5u,10u,15u})
        for (unsigned fault = 0; fault < 7; ++fault) {
            test_mem memory;
            for (unsigned a = 0x8000; a < 0xb000; ++a) memory.data[a] = (a*37+19) & 255;
            memory.write_code(0x1000,{reinterpret_cast<const std::uint8_t *>(pixel.data()),
                reinterpret_cast<const std::uint8_t *>(pixel.data()+pixel.size())});
            test_mem reference = memory;
            alignas(8) unsigned state[256]{};
            for (unsigned r = 0; r < 15; ++r) state[r] = r*73+11;
            state[0] = fault == 4 ? 0xaffd : 0xa000;
            state[2] = fault == 3 ? 0x8ffe : fault == 5 ? 0x8001 : 0x8000;
            state[3] = iterations; state[7] = 0xfc; state[15] = 0x1000;
            state[S::MODE/4] = 16; state[S::CPSR/4] = 0x30 | (flags<<28) | (fault==6 ? 0x200 : 0);
            state[S::TFLAG/4] = 1; state[S::NIRQ/4] = 1;
            state[S::NUM_INSTRS_TO_EXECUTE/4] = UINT32_MAX;
            state[S::NFLAG/4] = (flags>>3)&1; state[S::ZFLAG/4] = (flags>>2)&1;
            state[S::CFLAG/4] = (flags>>1)&1; state[S::VFLAG/4] = flags&1;
            r12l1::exclusive_monitor mon(1); auto cpu = make_cpu(reference,mon);
            for (unsigned r=0;r<16;++r) cpu->set_reg(r,state[r]);
            cpu->set_cpsr(state[S::CPSR/4]);
            r12l1::tlb tlb(12);
            tlb.add(0x8000,memory.data.data()+0x8000,fault==1?0:3);
            tlb.add(0xa000,memory.data.data()+0xa000,fault==2?1:3);
            memory_experiment::view view;
            const auto pointer = view.enter(1,1,[&] {return std::vector<memory_experiment::binding>{
                {0x8000,static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(memory.data.data()+0x8000)),fault==1?0u:3u},
                {0xa000,static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(memory.data.data()+0xa000)),fault==2?1u:3u}};});
            state[S::AOT_TLB/4] = policy ? static_cast<unsigned>(pointer)
                : reinterpret_cast<std::uintptr_t>(tlb.entries);
            g_test_mem = &memory; g_all_memory_helper_calls = 0;
            // A proved finite loop completes even with an expired watchdog.
            watchdog::request = 1;
            const auto progress = js_run_aot_wasm(bytes.data(),bytes.size(),
                reinterpret_cast<std::uint8_t *>(state),sizeof(state));
            watchdog::request = 0;
            if (progress < 0 || g_all_memory_helper_calls || (!fault && (progress!=1 || state[3]))) return false;
            if (reference_to(*cpu,state) < 0 || memory.data != reference.data) {
                printf("FAIL finite Thumb loop policy=%u n=%u flags=%u fault=%u pc=%x\n",
                    policy,iterations,flags,fault,state[15]); return false;
            }
            ++checks;
        }
    }
    // Recognition is based on register effects, not a specific counter register.
    for (unsigned r=0;r<8;++r) {
        if (translate({static_cast<std::uint16_t>(0x3801 | (r<<8)),0xd1fd}).proved_terminating_loops != 1) return false;
        if (translate({static_cast<std::uint16_t>(0x1e40 | (r<<3) | r),0xd1fd}).proved_terminating_loops != 1) return false;
        for (auto write : {static_cast<std::uint16_t>(0x2000 | (r<<8)),
                static_cast<std::uint16_t>(0x6800 | r),static_cast<std::uint16_t>(0x0000 | r)}) {
            if (translate({write,static_cast<std::uint16_t>(0x3801 | (r<<8)),0xd1fc}).proved_terminating_loops
                    || translate({write,static_cast<std::uint16_t>(0x1e40 | (r<<3) | r),0xd1fc}).proved_terminating_loops) return false;
        }
    }
    for (const std::vector<std::uint16_t> code : {
            std::vector<std::uint16_t>{0x3802,0xd1fd}, {0x3801,0xd0fd},
            {0x3801,0xd1fe}, {0x3801}, {0xdf00,0x3801,0xd1fc},
            {0xe000,0x3801,0xd1fc}, {0x4780,0x3801,0xd1fc}, {0xb401,0x3801,0xd1fc}}) {
        if (translate(code).proved_terminating_loops) return false;
    }
    tracking::unsafe_code_mode = 0;
    if (translate(pixel).proved_terminating_loops || translate(pixel).end_address != 0x1008) return false;
    printf("PASS finite Thumb loops: %u interpreter state/flags/memory comparisons, deferred misses, watchdog and proof rejection\n",checks);
#endif
    return true;
}
