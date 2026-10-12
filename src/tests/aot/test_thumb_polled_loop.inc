// Backedges can reuse locals and direct mappings until a watchdog/stop/IRQ
// request. Scalar faults leave before the access, including after earlier writes.
static bool test_thumb_polled_loops() {
#ifdef __EMSCRIPTEN__
    using S = state_offsets;
    namespace tracking = eka2l1::common::code_tracking;
    struct restore {
        unsigned mode=memory_experiment::mode, unsafe=tracking::unsafe_code_mode;
        ~restore() { memory_experiment::mode=mode; tracking::unsafe_code_mode=unsafe;
            watchdog::request=0; g_test_mem=nullptr; }
    } saved;
    memory_experiment::mode=2; tracking::unsafe_code_mode=3;
    const std::uint16_t code[]={0x6808,0x3001,0x6008,0x3104,0x4291,0xd3f9,0x4770};
    const auto translate=[](const std::uint16_t *p,unsigned size) {
        return translate_thumb_block(reinterpret_cast<const std::uint8_t *>(p),size,
            0x1000,nullptr,nullptr,true,true,true);
    };
    const auto module=[](const translate_result &tr) {
        return build_wasm_module({tr.func}, {{"env","tlb_read32",2,true},
            {"env","tlb_write32",3,false},{"env","tlb_read8",2,true},
            {"env","tlb_write8",3,false},{"env","tlb_read16",2,true},{"env","tlb_write16",3,false}});
    };
    const auto tr=translate(code,sizeof(code));
    if (tr.watchdog_safepoints!=1 || tr.proved_terminating_loops) return false;
    const auto bytes=module(tr);
    unsigned checks=0;
    for(unsigned iterations:{1u,2u,17u}) for(unsigned event=0;event<5;++event)
    for(unsigned fault=0;fault<6;++fault) for(unsigned flags:{0u,5u,10u,15u}) {
        test_mem memory;
        for(unsigned a=0x8000;a<0xa000;++a)memory.data[a]=(a*37+19)&255;
        memory.write_code(0x1000,{reinterpret_cast<const std::uint8_t *>(code),
            reinterpret_cast<const std::uint8_t *>(code)+sizeof(code)});
        test_mem reference=memory;
        alignas(8) unsigned state[256]{};
        state[1]=fault==3?0x8ffc:fault==4?0x8001:0x8000;
        state[2]=state[1]+iterations*4; state[14]=0x3001; state[15]=0x1000;
        state[S::MODE/4]=16; state[S::CPSR/4]=0x30|(flags<<28)|(fault==5?0x200:0)|(event==4?0x80:0);
        state[S::TFLAG/4]=1; state[S::NIRQ/4]=event>=3?0:1;
        state[S::NUM_INSTRS_TO_EXECUTE/4]=event==2?0:UINT32_MAX;
        state[S::NFLAG/4]=(flags>>3)&1;state[S::ZFLAG/4]=(flags>>2)&1;
        state[S::CFLAG/4]=(flags>>1)&1;state[S::VFLAG/4]=flags&1;
        r12l1::exclusive_monitor mon(1);auto cpu=make_cpu(reference,mon);
        for(unsigned r=0;r<16;++r)cpu->set_reg(r,state[r]);cpu->set_cpsr(state[S::CPSR/4]);
        memory_experiment::view view;
        state[S::AOT_TLB/4]=view.enter(1,1,[&]{return std::vector<memory_experiment::binding>{
            {0x8000,static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(memory.data.data()+0x8000)),
                fault==1?0u:fault==2?1u:3u}};});
        g_test_mem=&memory;g_all_memory_helper_calls=0;watchdog::request=event==1;
        const auto progress=js_run_aot_wasm(bytes.data(),bytes.size(),reinterpret_cast<std::uint8_t *>(state),sizeof(state));
        watchdog::request=0;
        const bool interrupted=iterations>1 && event>=1 && event<=3;
        if(progress<0 || g_all_memory_helper_calls || (!fault && state[15]!=(interrupted?0x1000u:0x3000u))
                || reference_to(*cpu,state)<0 || memory.data!=reference.data) {
            printf("FAIL polled Thumb loop n=%u event=%u fault=%u flags=%u pc=%x helpers=%u\n",
                iterations,event,fault,flags,state[15],g_all_memory_helper_calls);return false;
        }
        ++checks;
    }
    // A conditional RAM helper and a fused successor must preserve loop-carried
    // registers, flags, stores and pre-fault state over multiple iterations.
    std::vector<std::uint16_t> calling(36,0x46c0);
    const std::uint16_t body[]={0x8830,0x4621,0xf000,0xf81c,0x3402,0x3602,0x3d01,0xd1f7,0x4738};
    std::copy(std::begin(body),std::end(body),calling.begin());
    const std::uint16_t helper[]={0x2800,0xd000,0x8008,0x4770};
    std::copy(std::begin(helper),std::end(helper),calling.begin()+32);
    const auto *calling_bytes=reinterpret_cast<const std::uint8_t *>(calling.data());
    leaf_resolver resolve=[&](unsigned pc) { return pc>=0x1000 && pc<0x1000+calling.size()*2
        ? std::vector<std::uint8_t>(calling_bytes+pc-0x1000,calling_bytes+calling.size()*2)
        : std::vector<std::uint8_t>{}; };
    code_window empty{};
    unsigned call_checks=0;
    for(bool fused:{false,true}) {
        const std::uint16_t wrapper=0xe006; // branch from 0xff0 to the loop at 0x1000
        const unsigned entry=fused?0xff0:0x1000;
        const auto call_loop=translate_thumb_block(fused?reinterpret_cast<const std::uint8_t *>(&wrapper):calling_bytes,
            fused?sizeof(wrapper):calling.size()*2,entry,nullptr,nullptr,true,true,true,&empty,&resolve);
        if(call_loop.watchdog_safepoints!=1 || call_loop.inlined_thumb_helpers!=1
                || (fused && !call_loop.fused_edges))return false;
        const auto call_bytes=module(call_loop);
        for(unsigned iterations:{1u,2u,17u})for(unsigned event:{0u,1u})
        for(unsigned fault=0;fault<7;++fault)for(unsigned flags:{0u,5u,10u,15u}) {
            test_mem memory;
            for(unsigned a=0x8000;a<0xc000;++a)memory.data[a]=(a*37+19)&255;
            memory.write32(0xa000,0x12340000); // transparent, then opaque pixel
            memory.write_code(0xff0,{0x06,0xe0});
            memory.write_code(0x1000,{calling_bytes,calling_bytes+calling.size()*2});
            test_mem reference=memory;
            alignas(8) unsigned state[256]{};
            state[4]=fault==4?0x8fff:fault==5?0x8001:0x8000;
            state[6]=fault==3?0xaffe:0xa000;state[5]=iterations;state[7]=0x3001;
            state[14]=0x4567;state[15]=entry;state[S::MODE/4]=16;
            state[S::CPSR/4]=0x30|(flags<<28)|(fault==6?0x200:0);
            state[S::TFLAG/4]=1;state[S::NIRQ/4]=1;state[S::NUM_INSTRS_TO_EXECUTE/4]=UINT32_MAX;
            state[S::NFLAG/4]=(flags>>3)&1;state[S::ZFLAG/4]=(flags>>2)&1;
            state[S::CFLAG/4]=(flags>>1)&1;state[S::VFLAG/4]=flags&1;
            r12l1::exclusive_monitor mon(1);auto cpu=make_cpu(reference,mon);
            for(unsigned r=0;r<16;++r)cpu->set_reg(r,state[r]);cpu->set_cpsr(state[S::CPSR/4]);
            memory_experiment::view view;
            state[S::AOT_TLB/4]=view.enter(1,1,[&]{return std::vector<memory_experiment::binding>{
                {0x8000,static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(memory.data.data()+0x8000)),fault==1?0u:fault==2?1u:3u},
                {0xa000,static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(memory.data.data()+0xa000)),3u}};});
            g_test_mem=&memory;g_all_memory_helper_calls=0;watchdog::request=event;
            const auto progress=js_run_aot_wasm(call_bytes.data(),call_bytes.size(),
                reinterpret_cast<std::uint8_t *>(state),sizeof(state));
            watchdog::request=0;
            if(progress<0 || g_all_memory_helper_calls || reference_to(*cpu,state)<0 || memory.data!=reference.data) {
                printf("FAIL polled RAM call n=%u event=%u fault=%u flags=%u pc=%x\n",iterations,event,fault,flags,state[15]);return false;
            }
            ++call_checks;
        }
    }
    printf("PASS polled RAM calls: %u independent interpreter comparisons\n",call_checks);
    // The real external watchdog interrupts an otherwise infinite guest loop.
    const std::uint16_t infinite[]={0x3001,0xe7fd};
    const auto cycle=translate(infinite,sizeof(infinite));
    if(cycle.watchdog_safepoints!=1 || cycle.proved_terminating_loops)return false;
    const auto cycle_bytes=module(cycle);
    alignas(8) unsigned state[256]{};
    state[15]=0x1000;state[S::TFLAG/4]=1;state[S::CPSR/4]=0x30;
    state[S::NIRQ/4]=1;state[S::NUM_INSTRS_TO_EXECUTE/4]=UINT32_MAX;
    std::atomic<std::uint32_t> handshake{0};watchdog::request=0;
    watchdog_test_arm(reinterpret_cast<std::uintptr_t>(&watchdog::request),reinterpret_cast<std::uintptr_t>(&handshake));
    const auto progress=watchdog_test_run(cycle_bytes.data(),cycle_bytes.size(),
        reinterpret_cast<std::uintptr_t>(state),reinterpret_cast<std::uintptr_t>(&handshake));
    if(progress!=1 || !state[0] || state[15]!=0x1000 || !watchdog::requested())return false;
    // Callback-bearing bodies cannot retain mappings across the loop edge.
    const std::uint16_t callback[]={0xc101,0xe7fd};
    if(translate(callback,sizeof(callback)).watchdog_safepoints)return false;
    for(unsigned mode:{0u,2u})for(unsigned unsafe:{0u,3u}) {
        if(mode==2 && unsafe==3)continue;
        memory_experiment::mode=mode;tracking::unsafe_code_mode=unsafe;
        if(translate(code,sizeof(code)).watchdog_safepoints)return false;
    }
    printf("PASS polled Thumb loops: %u interpreter comparisons, external atomic watchdog, callback rejection\n",checks);
#endif
    return true;
}
