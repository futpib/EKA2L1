// Deferred scalar misses keep the exact pre-instruction state; ordinary direct
// stores continue without callback or dispatcher overhead.
static bool test_thumb_store_continuation() {
#ifdef __EMSCRIPTEN__
    using S = state_offsets;
    namespace tracking = eka2l1::common::code_tracking;
    struct restore {
        unsigned mode = memory_experiment::mode, unsafe = tracking::unsafe_code_mode;
        ~restore() { memory_experiment::mode = mode; tracking::unsafe_code_mode = unsafe;
            watchdog::request = 0; g_test_mem = nullptr; }
    } saved;
    memory_experiment::mode = 2;
    tracking::unsafe_code_mode = 3;
    const std::vector<std::vector<std::uint16_t>> programs = {
        {0x6808,0x3001,0x6008,0x684a,0x3201,0x604a,0x4770},
        {0x8808,0x2800,0xd001,0x8008,0x3001,0x3102,0x4770},
        {0x9000,0x9a00,0x3201,0x9201,0x4770},
        {0x5088,0x588b,0x5488,0x5c8b,0x3001,0x4770},
        {0x5288,0x568b,0x5488,0x5e8b,0x3001,0x4770},
        {0x6008,0x3104,0x7008,0x780a,0x3101,0x700a,0x4770},
        {0xb40f,0xbc0f,0x4770}, // PUSH/POP spanning multiple words
        {0xb500,0xbd00},        // LR saved and restored directly into PC
        {0xc105,0x3908,0xc905,0x4770} // STMIA/LDMIA with base writeback
    };
    unsigned checks = 0;
    for (bool rom_recovery : {false, true})
    for (const auto &code : programs) {
        const auto tr = translate_thumb_block(reinterpret_cast<const std::uint8_t *>(code.data()),
            code.size()*2,0x1000,nullptr,nullptr,true,!rom_recovery,true,nullptr,nullptr,rom_recovery);
        if (!tr.complete || tr.end_address != 0x1000 + code.size()*2) return false;
        const auto bytes = build_wasm_module({tr.func}, {{"env","tlb_read32",2,true},
            {"env","tlb_write32",3,false},{"env","tlb_read8",2,true},
            {"env","tlb_write8",3,false},{"env","tlb_read16",2,true},{"env","tlb_write16",3,false}});
        for (unsigned input : {0u,1u,0xffffffffu})
        for (unsigned flags : {0u,5u,10u,15u})
        for (unsigned fault = 0; fault < 6; ++fault) {
            test_mem memory;
            for (unsigned a=0x8000;a<0xa000;++a) memory.data[a]=(a*37+19)&255;
            memory.write_code(0x1000,{reinterpret_cast<const std::uint8_t *>(code.data()),
                reinterpret_cast<const std::uint8_t *>(code.data()+code.size())});
            memory.write32(0x8000,input);
            test_mem reference = memory;
            alignas(8) unsigned state[256]{};
            for (unsigned r=0;r<15;++r) state[r]=r*73+11;
            state[0]=input; state[1]=fault==3?0x8ffc:fault==4?0x8001:0x8000;
            state[2]=4; state[13]=state[1]; state[14]=0x3001; state[15]=0x1000;
            if (code[0] == 0xb40f) state[13] += 16;
            if (code[0] == 0xb500) state[13] += 4;
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
                    fault==1?0u:fault==2?1u:3u}};});
            g_test_mem=&memory; g_all_memory_helper_calls=0; watchdog::request=1;
            const auto progress=js_run_aot_wasm(bytes.data(),bytes.size(),
                reinterpret_cast<std::uint8_t *>(state),sizeof(state));
            watchdog::request=0;
            if (progress<0 || g_all_memory_helper_calls || (!fault && (state[15] & ~1u) != (code[0] == 0x8808 && !input ? 0x100a : 0x3000))
                    || reference_to(*cpu,state)<0 || memory.data!=reference.data) {
                printf("FAIL Thumb store continuation first=%x input=%x flags=%u fault=%u pc=%x helpers=%u progress=%d r0=%x r1=%x r2=%x\n",
                    code[0],input,flags,fault,state[15],g_all_memory_helper_calls,progress,state[0],state[1],state[2]); return false;
            }
            ++checks;
        }
    }
    // The mutation-compatible path still returns at its first store.
    tracking::unsafe_code_mode=0;
    const std::uint16_t code[]={0x6008,0x3001,0x6048,0x4770};
    if (translate_thumb_block(reinterpret_cast<const std::uint8_t *>(code),sizeof(code),
            0x1000,nullptr,nullptr,true,true,true).end_address!=0x1002) return false;
    printf("PASS Thumb RAM continuation and ROM compiled recovery: %u interpreter state/flags/memory comparisons\n",checks);
#endif
    return true;
}
