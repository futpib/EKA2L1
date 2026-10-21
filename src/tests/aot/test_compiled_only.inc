static bool test_shared_compiled_code() {
    test_mem memory;
    r12l1::exclusive_monitor monitor(1);
    auto cpu = make_cpu(memory, monitor);
    unsigned space = 1;
    const auto *backing = memory.data.data() + 0x1000;
    cpu->resolve_code = [&](unsigned address, core::code_mapping &view) {
        view = {space, memory.data.data() + address + (space == 2 ? 4 : 0), 4096}; return true;
    };
    validated_code_cache cache;
    core::code_mapping view{space, backing, 16};
    auto &original = cache.insert(0x1000, view, 4);
    cache.add_dependency(original, 0x2000, memory.data.data() + 0x2000, {0, 0, 0, 0});
    if (!cache.attach(original.version, runner_test_function)) return false;
    space = view.address_space = 2;
    if (cache.share(0x1000, view, *cpu, 1) || cache.share(0x1001, view, *cpu)) return false;
    auto other = view; other.bytes += 4;
    memory.data[0x1004] = 1;
    if (cache.share(0x1000, other, *cpu)) return false;
    memory.data[0x1004] = 0;
    view = other;
    memory.data[0x2004] = 1;
    if (cache.share(0x1000, view, *cpu)) return false;
    memory.data[0x2004] = 0;
    if (cache.share(0x1000, view, *cpu) != runner_test_function) return false;
    auto *second = cache.find(0x1000, view, cpu.get());
    if (!second || second->function != original.function) return false;
    cache.retire(original);
    if (!cache.collect_retired().empty()) return false;
    space = view.address_space = 3;
    view.bytes = backing;
    if (cache.share(0x1000, view, *cpu) != runner_test_function) return false;
    auto *third = cache.find(0x1000, view, cpu.get());
    if (!third) return false;
    cache.retire(*second);
    if (!cache.collect_retired().empty()) return false;
    cache.retire(*third);
    const auto released = cache.collect_retired();
    if (released.size() != 1 || released[0] != runner_test_function) return false;
    printf(" PASS shared compiled code: exact PC/mode/policy/bytes/dependencies, distinct process backing and release after final owner\n");
    return true;
}

static bool test_cold_module_lifetime() {
#ifdef __EMSCRIPTEN__
    const auto failed = [](unsigned line) { printf(" FAIL cold module lifetime at line %u\n", line); return false; };
    struct restore {
        unsigned memory = memory_experiment::mode;
        bool hot = hot_compilation_enabled;
        ~restore() { memory_experiment::mode = memory; hot_compilation_enabled = hot;
            global_registry().unregister_function(0x62000); global_registry().unregister_function(0x63001); }
    } saved;
    memory_experiment::mode = 0;
    hot_compilation_enabled = true;
    const unsigned arm_code = 0xe3a00007;
    const unsigned short thumb_code = 0x2009;
    auto arm = translate_arm_block(reinterpret_cast<const std::uint8_t *>(&arm_code), 4, 0x62000,
        nullptr, nullptr, true, false, false);
    auto thumb = translate_thumb_block(reinterpret_cast<const std::uint8_t *>(&thumb_code), 2, 0x63000,
        nullptr, nullptr, true, false, false);
    thumb.func.export_name = "f_" + std::to_string(0x63001);
    stage_aot_module(build_wasm_module({arm.func, thumb.func}, {}), "test-cold-module");
    instantiate_staged_modules();
    const auto function = global_registry().lookup(0x62000);
    const auto thumb_function = global_registry().lookup(0x63001);
    if (!function || !thumb_function || function == thumb_function) return failed(__LINE__);
    test_mem memory;
    r12l1::exclusive_monitor monitor(1);
    auto cpu = make_cpu(memory, monitor);
    auto *state = matched_kernel_access::state(*cpu);
    state->NumInstrsToExecute = 1; state->Reg[15] = 0x62000; state->TFlag = 0;
    if (execute_single(state, function) != 1 || state->Reg[0] != 7 || state->Reg[15] != 0x62004) return failed(__LINE__);
    state->Reg[15] = 0x63000; state->TFlag = 1;
    if (execute_single(state, thumb_function) != 1 || state->Reg[0] != 9 || state->Reg[15] != 0x63002) return failed(__LINE__);
    // Connected cold entries are installed together with their context.
    // Retiring one entry must preserve the other body in the shared module.
    cpu->code_address_space = 818;
    queue_precompile_image(818, 0x72000, memory.data.data() + 0x72000, 0x280, {});
    memory.write32(0x72000, 0xe3a00007); memory.write32(0x72004, 0xea00007d);
    memory.write32(0x72200, 0xe3a00009); memory.write32(0x72204, 0xef0000ff);
    state->Reg[15] = 0x72000; state->TFlag = 0; state->Cpsr = 16;
    const auto shared = compile_missing(state);
    if (!shared || execute_single(state, shared) != 1 || state->Reg[15] != 0x72200) return failed(__LINE__);
    const auto second = compile_missing(state);
    if (!second) return failed(__LINE__);
    if (!EM_ASM_INT({return wasmTable.get($0) !== wasmTable.get($1);},
            reinterpret_cast<std::uintptr_t>(shared), reinterpret_cast<std::uintptr_t>(second))) return failed(__LINE__);
    state->Reg[15] = 0x72208;
    const auto survivor = lookup_compiled(state);
    if (!survivor) return failed(__LINE__);
    state->Reg[15] = 0x72200;
    invalidate_ram_code(0x72000, 8); prepare_compiled_code(*cpu);
    if (lookup_compiled(state) != second || execute_single(state, second) != 0
            || state->Reg[0] != 9 || !(state->aot_exit & svc_pending)) return failed(__LINE__);
    invalidate_ram_code(0x72200, 8); prepare_compiled_code(*cpu);
    memory.write32(0x72200, 0xe3a0000a); state->Reg[15] = 0x72200;
    const auto replacement = compile_missing(state);
    if (!replacement || execute_single(state, replacement) != 0 || state->Reg[0] != 10) return failed(__LINE__);
    // Different processes may map different bytes at the same virtual PC.
    // Both versions must retain their own semantics.
    memory.write32(0x74c00, 0xe3a0000b); memory.write32(0x74c04, 0xef0000ff);
    state->Reg[15] = 0x74c00;
    const auto first_version = compile_missing(state);
    test_mem other_memory;
    r12l1::exclusive_monitor other_monitor(1);
    auto other_cpu = make_cpu(other_memory, other_monitor);
    other_cpu->code_address_space = 819;
    auto *other_state = matched_kernel_access::state(*other_cpu);
    other_memory.write32(0x74c00, 0xe3a0000c); other_memory.write32(0x74c04, 0xef0000ff);
    other_state->NumInstrsToExecute = 1; other_state->Cpsr = 16; other_state->TFlag = 0; other_state->Reg[15] = 0x74c00;
    const auto other_version = compile_missing(other_state);
    if (!first_version || !other_version || first_version == other_version) {
        printf(" FAIL process versions first=%p other=%p pc=%x/%x thumb=%u/%u mode=%u unsafe=%u\n", first_version, other_version, state->Reg[15], other_state->Reg[15], state->TFlag, other_state->TFlag, memory_experiment::mode, eka2l1::common::code_tracking::unsafe_code_mode);
        return failed(__LINE__);
    }
    state->Reg[15] = other_state->Reg[15] = 0x74c00;
    if (execute_single(state, first_version) || state->Reg[0] != 11
            || execute_single(other_state, other_version) || other_state->Reg[0] != 12) return failed(__LINE__);
    // Force module merging after slots have been retired and reused. Version
    // checks must reject old definitions and preserve both process contexts.
    for (unsigned n = 0; n < 72; ++n) {
        const unsigned address = 0x72080 + n * 4;
        memory.write32(address, 0xef0000ff);
        state->Reg[15] = address;
        if (!compile_missing(state)) return failed(__LINE__);
    }
    state->Reg[15] = other_state->Reg[15] = 0x74c00;
    if (execute_single(state, first_version) || state->Reg[0] != 11
            || execute_single(other_state, other_version) || other_state->Reg[0] != 12) return failed(__LINE__);
    invalidate_ram_code(0x72000, 0x280); prepare_compiled_code(*cpu);
    if (!EM_ASM_INT({return WebAssembly.Table.prototype.get.call(wasmTable,$0) === null
            && WebAssembly.Table.prototype.get.call(wasmTable,$1) === null;},
            reinterpret_cast<std::uintptr_t>(survivor), reinterpret_cast<std::uintptr_t>(replacement))) return failed(__LINE__);
    printf(" PASS cold modules: ARM/Thumb exports, partial retirement and callable-slot reuse\n");
#endif
    return true;
}

static bool test_compiled_only() {
#ifdef __EMSCRIPTEN__
    struct restore {
        unsigned memory = memory_experiment::mode;
        bool hot = hot_compilation_enabled;
        unsigned unsafe = eka2l1::common::code_tracking::unsafe_code_mode;
        ~restore() { memory_experiment::mode = memory; hot_compilation_enabled = hot;
            eka2l1::common::code_tracking::unsafe_code_mode = unsafe; }
    } saved;
    hot_compilation_enabled = true;
    unsigned checks = 0;
    for (unsigned scenario = 0; scenario < 3; ++scenario) {
        memory_experiment::mode = 0;
        test_mem memory;
        r12l1::exclusive_monitor monitor(1);
        auto cpu = make_cpu(memory, monitor);
        cpu->set_asid(810 + scenario);
        cpu->code_address_space = 810 + scenario;
        memory.write32(0x8000, 5);
        monitor.read_32bit = [&](core *, address a, unsigned *out) {
            *out = memory.read32(a); return true;
        };
        monitor.write_32bit = [&](core *, address a, unsigned value, unsigned expected) {
            if (memory.read32(a) != expected) return 0;
            memory.write32(a, value); return 1;
        };
        std::vector<unsigned> code{0xe1912f9f}; // LDREX r2, [r1]
        if (scenario == 1) code.push_back(0xef000005); // SVC clears the reservation
        if (scenario == 2) code.push_back(0xe5810000); // STR r0, [r1] changes the saved value
        code.insert(code.end(), {0xe2822001, 0xe1813f92, 0xe5914000, 0xef0000ff});
        memory.write_code(0x6d000, {reinterpret_cast<const std::uint8_t *>(code.data()),
            reinterpret_cast<const std::uint8_t *>(code.data() + code.size())});
        unsigned calls = 0;
        cpu->system_call_handler = [&](unsigned number) { ++calls; if (number == 255) cpu->stop(); };
        cpu->exception_handler = [&](auto, auto) { cpu->stop(); return false; };
        cpu->set_reg(0, 7); cpu->set_reg(1, 0x8000); cpu->set_pc(0x6d000); cpu->set_cpsr(16);
        watchdog::request = 0;
        cpu->run(0);
        const unsigned expected = scenario == 0 ? 6 : scenario == 1 ? 5 : 7;
        if (cpu->get_reg(2) != 6 || cpu->get_reg(3) != unsigned(scenario != 0)
                || cpu->get_reg(4) != expected || memory.read32(0x8000) != expected
                || calls != (scenario == 1 ? 2u : 1u)
                || matched_kernel_access::state(*cpu)->trans_cache_buf) {
            printf(" FAIL compiled exclusive scenario=%u r2=%u r3=%u r4=%u memory=%u calls=%u\n",
                scenario, cpu->get_reg(2), cpu->get_reg(3), cpu->get_reg(4), memory.read32(0x8000), calls);
            return false;
        }
        ++checks;
    }
    // Every direct-memory proof fails. The precise compiled recovery must
    // perform both the load and store through real callbacks without decoding.
    memory_experiment::mode = 2;
    test_mem memory;
    r12l1::exclusive_monitor monitor(1);
    auto cpu = make_cpu(memory, monitor);
    cpu->set_asid(814);
    cpu->code_address_space = 814;
    std::vector<memory_experiment::page> pages(1 << 20);
    memory_experiment::direct_view view{0, 0, 0,
        static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(pages.data())), 0, 0};
    cpu->experimental_pointer = reinterpret_cast<std::uintptr_t>(&view);
    cpu->experimental_memory = [&](bool) { return reinterpret_cast<std::uintptr_t>(&view); };
    memory.write32(0x8000, 5);
    const unsigned short code[] = {0x680a, 0x3201, 0x600a, 0xdfff};
    memory.write_code(0x6e000, {reinterpret_cast<const std::uint8_t *>(code),
        reinterpret_cast<const std::uint8_t *>(code) + sizeof(code)});
    unsigned calls = 0;
    cpu->system_call_handler = [&](unsigned n) { ++calls; if (n == 255) cpu->stop(); };
    cpu->exception_handler = [&](auto, auto) { cpu->stop(); return false; };
    cpu->set_reg(1, 0x8000); cpu->set_pc(0x6e000); cpu->set_cpsr(48);
    cpu->run(0);
    auto *state = matched_kernel_access::state(*cpu);
    if (cpu->get_reg(2) != 6 || memory.read32(0x8000) != 6 || calls != 1 || state->trans_cache_buf) {
        printf(" FAIL compiled memory recovery r2=%u memory=%u calls=%u\n",
            cpu->get_reg(2), memory.read32(0x8000), calls);
        return false;
    }
    ++checks;
    // Scalar recovery must preserve indexed writeback, signed loads, loaded
    // PCs, and the state seen by callbacks that stop the current slice.
    const unsigned scalar_ops[]{0xe4910004,0xe4810004,0xe5b10004,0xe5a10004,
        0xe4d10001,0xe4c10001,0xe5f10001,0xe5e10001,0xe0d100b2,0xe0c100b2,
        0xe1f100b2,0xe1e100b2,0xe0d100d1,0xe0d100f2,0xe1f100d1,0xe1f100f2,
        0xe491f004,0xe5b1f004};
    for(unsigned opcode:scalar_ops) for(unsigned path=0;path<3;++path) {
        memory_experiment::mode=path?2:0;
        const bool stop=path==2;
        test_mem actual,expected;
        const unsigned pc=0x80000;
        actual.write32(pc,opcode);actual.write32(pc+4,0xef0000ff);
        actual.write32(0x81000,0xef0000ff);
        const unsigned data=((opcode>>12)&15)==15?0x81000:0x81828384;
        actual.write32(0x8000,data);actual.write32(0x8004,data);
        expected=actual;
        r12l1::exclusive_monitor actual_monitor(1),expected_monitor(1);
        auto compiled=make_cpu(actual,actual_monitor),reference=make_cpu(expected,expected_monitor);
        compiled->code_address_space=1000+checks;
        compiled->experimental_pointer=reinterpret_cast<std::uintptr_t>(&view);
        compiled->experimental_memory=[&](bool){return reinterpret_cast<std::uintptr_t>(&view);};
        if(!path)compiled->set_tlb_page(0x8000,actual.data.data()+0x8000,prot_read_write);
        using observation=std::array<unsigned,3>;
        std::vector<observation> actual_calls,expected_calls;
        for(auto *core:{compiled.get(),reference.get()}) {
            core->set_pc(pc);core->set_cpsr(0xa0000010);
            core->set_reg(0,0x12345678);core->set_reg(1,0x8000);
            auto wrap=[&,core](auto function) {
                return [&,core,function](address a,auto *value) {
                    auto &events=core==compiled.get()?actual_calls:expected_calls;
                    events.push_back({a,core->get_reg(1),core->get_pc()});
                    if(stop&&core==compiled.get())core->stop();
                    return function(a,value);
                };
            };
            core->read_32bit=wrap(core->read_32bit);core->write_32bit=wrap(core->write_32bit);
            core->read_16bit=wrap(core->read_16bit);core->write_16bit=wrap(core->write_16bit);
            core->read_8bit=wrap(core->read_8bit);core->write_8bit=wrap(core->write_8bit);
        }
        bool reference_done=false;
        compiled->system_call_handler=[&](unsigned n){if(n==255)compiled->stop();};
        reference->system_call_handler=[&](unsigned n){if(n==255)reference_done=true;};
        reference->interpret_step_for_test();
        if(!stop)reference->interpret_step_for_test();
        watchdog::request=0;compiled->run(0);
        for(unsigned reg=0;reg<16;++reg)if(compiled->get_reg(reg)!=reference->get_reg(reg)) {
            printf(" FAIL scalar recovery op=%x path=%u r%u actual=%x expected=%x\n",
                opcode,path,reg,compiled->get_reg(reg),reference->get_reg(reg));return false;
        }
        if(actual.data!=expected.data||compiled->get_cpsr()!=reference->get_cpsr()
                ||(!stop&&!reference_done)||(path&&actual_calls!=expected_calls)
                ||matched_kernel_access::state(*compiled)->trans_cache_buf) {
            printf(" FAIL scalar recovery memory/flags/callback op=%x path=%u flags=%x/%x calls=%zu/%zu\n",
                opcode,path,compiled->get_cpsr(),reference->get_cpsr(),actual_calls.size(),expected_calls.size());
            for(const auto &event:actual_calls)printf(" actual %x %x %x\n",event[0],event[1],event[2]);
            for(const auto &event:expected_calls)printf(" expected %x %x %x\n",event[0],event[1],event[2]);return false;
        }
        ++checks;
    }
    // Identical code is copied into two address spaces with different data.
    // Reusing its native function must still use the active core's memory.
    memory_experiment::mode = 0;
    const unsigned shared_code[] = {0xe5910000, 0xef0000ff};
    test_mem first, second;
    r12l1::exclusive_monitor shared_monitor(1);
    std::uint64_t compiled = 0;
    for (unsigned n = 0; n < 2; ++n) {
        auto &mem = n ? second : first;
        mem.write_code(0x6f000, {reinterpret_cast<const std::uint8_t *>(shared_code),
            reinterpret_cast<const std::uint8_t *>(shared_code) + sizeof(shared_code)});
        mem.write32(0x8000, 33 + n);
        auto core = make_cpu(mem, shared_monitor);
        core->set_asid(816 + n);
        core->code_address_space = 816 + n;
        core->set_reg(1, 0x8000); core->set_pc(0x6f000); core->set_cpsr(16);
        core->system_call_handler = [&](unsigned number) { if (number == 255) core->stop(); };
        core->run(0);
        if (core->get_reg(0) != 33 + n || matched_kernel_access::state(*core)->trans_cache_buf
                || (n && compiled_function_count() != compiled)) {
            printf(" FAIL shared execution process=%u r0=%u functions=%llu/%llu\n", n, core->get_reg(0),
                static_cast<unsigned long long>(compiled_function_count()), static_cast<unsigned long long>(compiled));
            return false;
        }
        compiled = compiled_function_count();
        ++checks;
    }
    cpu->set_pc(0x6e002); cpu->set_cpsr(0xa0000030); cpu->step();
    if (cpu->get_reg(2) != 7 || cpu->get_pc() != 0x6e004 || state->trans_cache_buf) { printf(" FAIL step register=%u pc=%x\n",cpu->get_reg(2),cpu->get_pc()); return false; }
    cpu->set_pc(0x6e006); cpu->step();
    if (calls != 2 || cpu->get_pc() != 0x6e008 || state->trans_cache_buf) { printf(" FAIL step syscall calls=%u pc=%x\n",calls,cpu->get_pc()); return false; }
    ++checks;
    // A branch that skips a memory helper must land past its callback guard,
    // while fallthrough completes the same instructions as the native oracle.
    for (unsigned thumb = 0; thumb < 2; ++thumb) for (unsigned zero = 0; zero < 2; ++zero) {
        memory_experiment::mode = 0;
        test_mem actual, expected;
        const std::vector<unsigned> arm_code{0x1a000002, 0xe5910000, 0xe2800001,
            0xe1a00000, 0xe3a02007, 0xef0000ff};
        const unsigned short thumb_code[]{0xd102, 0x6808, 0x3001, 0x46c0, 0x2207, 0xdfff};
        const auto *bytes = thumb ? reinterpret_cast<const std::uint8_t *>(thumb_code)
            : reinterpret_cast<const std::uint8_t *>(arm_code.data());
        const auto size = thumb ? sizeof(thumb_code) : arm_code.size() * 4;
        const unsigned pc = 0x7a000 + thumb * 0x1000;
        actual.write_code(pc, {bytes, bytes + size}); actual.write32(0x8000, 5);
        expected = actual;
        r12l1::exclusive_monitor actual_monitor(1), expected_monitor(1);
        auto compiled = make_cpu(actual, actual_monitor), reference = make_cpu(expected, expected_monitor);
        compiled->code_address_space = 850 + thumb * 2 + zero;
        bool reference_done = false;
        for (auto *core : {compiled.get(), reference.get()}) {
            core->set_pc(pc); core->set_cpsr(16 | (thumb << 5) | (zero << 30));
            core->set_reg(0, 10); core->set_reg(1, 0x8000);
        }
        compiled->system_call_handler = [&](unsigned number) { if (number == 255) compiled->stop(); };
        reference->system_call_handler = [&](unsigned number) { if (number == 255) reference_done = true; };
        for (unsigned n = 0; n < 8 && !reference_done; ++n) reference->interpret_step_for_test();
        if (!reference_done) return false;
        watchdog::request = 0; compiled->run(0);
        for (unsigned reg = 0; reg < 16; ++reg)
            if (compiled->get_reg(reg) != reference->get_reg(reg)) { printf(" FAIL branch thumb=%u zero=%u r%u actual=%x expected=%x\n",thumb,zero,reg,compiled->get_reg(reg),reference->get_reg(reg)); return false; }
        if (compiled->get_cpsr() != reference->get_cpsr()
                || matched_kernel_access::state(*compiled)->trans_cache_buf) return false;
        ++checks;
    }
    // A normal cold callback may continue into later memory instructions;
    // a callback that stops execution must finish only its current instruction.
    for (unsigned thumb = 0; thumb < 2; ++thumb) for (unsigned stop = 0; stop < 2; ++stop) {
        memory_experiment::mode = 0;
        test_mem mem;
        r12l1::exclusive_monitor mon(1);
        auto core = make_cpu(mem, mon);
        const unsigned pc = 0x7c000 + (thumb * 2 + stop) * 0x1000;
        const unsigned arm_code[]{0xe5910000, 0xe2800001, 0xe5810004, 0xe3a02007, 0xef0000ff};
        const unsigned short thumb_code[]{0x6808, 0x3001, 0x6048, 0x2207, 0xdfff};
        const auto *bytes = thumb ? reinterpret_cast<const std::uint8_t *>(thumb_code)
            : reinterpret_cast<const std::uint8_t *>(arm_code);
        mem.write_code(pc, {bytes, bytes + (thumb ? sizeof(thumb_code) : sizeof(arm_code))});
        mem.write32(0x8000, 5);
        core->code_address_space = 860 + thumb * 2 + stop;
        core->set_pc(pc); core->set_cpsr(16 | (thumb << 5)); core->set_reg(1, 0x8000);
        core->read_32bit = [&](address a, unsigned *value) {
            *value = mem.read32(a); if (stop) core->stop(); return true;
        };
        core->system_call_handler = [&](unsigned number) { if (number == 255) core->stop(); };
        watchdog::request = 0; core->run(0);
        if (core->get_reg(0) != (stop ? 5u : 6u) || core->get_reg(2) != (stop ? 0u : 7u)
                || mem.read32(0x8004) != (stop ? 0u : 6u)
                || core->get_pc() != pc + (thumb ? (stop ? 2 : 10) : (stop ? 4 : 20))) { printf(" FAIL callback thumb=%u stop=%u r0=%u r2=%u memory=%u pc=%x\n",thumb,stop,core->get_reg(0),core->get_reg(2),mem.read32(0x8004),core->get_pc()); return false; }
        ++checks;
    }
    {
        memory_experiment::mode = 0;
        test_mem memory;
        r12l1::exclusive_monitor monitor(1);
        auto cpu = make_cpu(memory, monitor);
        cpu->code_address_space = 950;
        memory.write32(0x79000, 0xe3a00007);
        memory.write32(0x79004, 0xef0000ff);
        const auto resolve = cpu->resolve_code;
        bool first = true;
        cpu->resolve_code = [&](address pc, core::code_mapping &view) {
            if (first) { first = false; watchdog::request = 1; }
            return resolve(pc, view);
        };
        cpu->system_call_handler = [&](unsigned) { cpu->stop(); };
        cpu->set_pc(0x79000); cpu->set_cpsr(16); watchdog::request = 0;
        cpu->run(0);
        if (cpu->guest_code_dispatched || cpu->get_pc() != 0x79000 || cpu->get_reg(0) != 0
                || !watchdog::requested()) return false;
        watchdog::request = 0;
        cpu->run(0);
        if (!cpu->guest_code_dispatched || cpu->get_reg(0) != 7) return false;
        ++checks;
    }
    // A whole-span miss must leave before any transfer or writeback. Its
    // precise compiled recovery retains the native callback/stop boundary.
    for (unsigned thumb = 0; thumb < 2; ++thumb)
    for (unsigned operation = 0; operation < (thumb ? 4u : 6u); ++operation) for (unsigned stop = 0; stop < 2; ++stop) {
        memory_experiment::mode = 2;
        eka2l1::common::code_tracking::unsafe_code_mode = 3;
        test_mem actual, expected;
        r12l1::exclusive_monitor actual_monitor(1), expected_monitor(1);
        auto compiled = make_cpu(actual, actual_monitor), reference = make_cpu(expected, expected_monitor);
        const unsigned pc = 0x90000 + (thumb * 12 + operation * 2 + stop) * 0x1000;
        const unsigned short operations[]{0xb403, 0xbc03, 0xc203, 0xca03};
        const unsigned short code[]{operations[operation % 4], 0xdfff};
        const unsigned arm_operations[]{0xe92d0003,0xe8bd0003,0xe8a20003,0xe8b20003,0xe8bd8001,0xe8b28001};
        const unsigned arm_code[]{arm_operations[operation],0xef0000ff};
        const auto *bytes=thumb?reinterpret_cast<const std::uint8_t *>(code):reinterpret_cast<const std::uint8_t *>(arm_code);
        actual.write_code(pc, {bytes,bytes+(thumb?sizeof(code):sizeof(arm_code))});
        actual.write32(0xc0000,0xef0000ff);
        actual.write32(0x8000, 5); actual.write32(0x8004, operation>=4?0xc0000:11); expected = actual;
        std::vector<memory_experiment::page> pages(1 << 20);
        memory_experiment::direct_view view{0, 0, 0,
            static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(pages.data())), 0, 0};
        compiled->experimental_pointer = reinterpret_cast<std::uintptr_t>(&view);
        compiled->experimental_memory = [&](bool) { return reinterpret_cast<std::uintptr_t>(&view); };
        compiled->code_address_space = 1200 + thumb * 12 + operation * 2 + stop;
        for (auto pair : {std::pair{compiled.get(), &actual}, std::pair{reference.get(), &expected}}) {
            auto *core = pair.first; auto *mem = pair.second;
            core->set_pc(pc); core->set_cpsr(16 | (thumb << 5));
            core->set_reg(0, 7); core->set_reg(1, 9); core->set_reg(2, 0x8000);
            core->set_sp(operation == 0 ? 0x8008 : 0x8000);
            core->read_32bit = [&,core,mem](address a, unsigned *value) {
                *value = mem->read32(a); if (stop) core->stop(); return true;
            };
            core->write_32bit = [&,core,mem](address a, unsigned *value) {
                mem->write32(a, *value); if (stop) core->stop(); return true;
            };
            core->system_call_handler = [core](unsigned) { core->stop(); };
        }
        const auto recoveries = compilation.single_functions.load();
        watchdog::request = 0; compiled->run(0);
        for (unsigned step = 0; step < (stop ? 1u : 2u); ++step) reference->interpret_step_for_test();
        for (unsigned reg = 0; reg < 16; ++reg) if (compiled->get_reg(reg) != reference->get_reg(reg)) {
            printf(" FAIL multi-register recovery thumb=%u operation=%u stop=%u r%u actual=%x expected=%x\n",
                thumb, operation, stop, reg, compiled->get_reg(reg), reference->get_reg(reg)); return false;
        }
        if (compiled->get_cpsr() != reference->get_cpsr() || actual.data != expected.data
                || compilation.single_functions.load() <= recoveries
                || matched_kernel_access::state(*compiled)->trans_cache_buf) return false;
        ++checks;
    }
    printf(" PASS compiled-only CPU: %u exclusive success/failure, SVC, deferred memory and stepping checks; no interpreter storage\n", checks);
#endif
    return true;
}

static bool test_rom_process_lifetime() {
#ifdef __EMSCRIPTEN__
    test_mem memory;
    r12l1::exclusive_monitor monitor(1);
    auto first = make_cpu(memory, monitor), second = make_cpu(memory, monitor);
    first->code_address_space = 900; second->code_address_space = 901;
    configure_hot_rom(memory.data.data() + 0x60000, 0x80040000, 0x4000, true);
    memory.write32(0x60000, 0xe3a00007); memory.write32(0x60004, 0xef0000ff);
    memory.write32(0x60020, 0xe3a00008); memory.write32(0x60024, 0xef0000ff);
    auto *a = matched_kernel_access::state(*first), *b = matched_kernel_access::state(*second);
    a->Reg[15] = 0x80040000; b->Reg[15] = 0x80040020;
    a->TFlag = b->TFlag = 0; a->Cpsr = b->Cpsr = 16;
    a->NumInstrsToExecute = b->NumInstrsToExecute = 1;
    const auto old = compile_missing(a), kept = compile_missing(b);
    if (!old || !kept) return false;
    const auto count = compiled_function_count();
    retire_process_code(900); prepare_compiled_code(*second);
    if (global_registry().lookup(0x80040000) != old || global_registry().lookup(0x80040020) != kept) return false;
    b->Reg[15] = 0x80040000;
    if (compile_missing(b) != old || execute_single(b,old) || b->Reg[0] != 7) return false;
    b->Reg[15] = 0x80040020;
    if (execute_single(b,kept) || b->Reg[0] != 8) return false;
    retire_process_code(901); prepare_compiled_code(*first);
    a->Reg[15] = 0x80040000;
    if (compile_missing(a) != old || execute_single(a,old) || a->Reg[0] != 7
            || compiled_function_count() != count) return false;
    global_registry().unregister_function(0x80040000);
    global_registry().unregister_function(0x80040020);
    EM_ASM({removeFunction($0);removeFunction($1);},
        reinterpret_cast<std::uintptr_t>(old), reinterpret_cast<std::uintptr_t>(kept));
    configure_hot_rom(nullptr,0,0,false);
    printf(" PASS immutable ROM lifetime: shared translations survive process exits without recompilation\n");
#endif
    return true;
}

static bool test_rom_translation_cache() {
#ifdef __EMSCRIPTEN__
    struct restore {
        unsigned mode = memory_experiment::mode;
        bool hot = hot_compilation_enabled;
        ~restore() { configure_rom_cache("", std::string(64, '0')); configure_hot_rom(nullptr, 0, 0, false);
            memory_experiment::mode = mode; hot_compilation_enabled = hot; }
    } saved;
    const auto fail = [](unsigned line) { printf(" FAIL ROM translation cache at line %u\n", line); return false; };
    memory_experiment::mode = 0;
    test_mem memory;
    r12l1::exclusive_monitor monitor(1);
    auto cpu = make_cpu(memory, monitor);
    memory.write32(0x60000, 0xe3a00007); memory.write32(0x60004, 0xef0000ff);
    memory.write32(0x60020, 0xdfff2009); // Thumb MOV r0,#9; SVC #255
    const std::string build(64, 'a'), path("/tmp/test-rom-cache.bin");
    if (configure_rom_cache("", "bad") || !configure_rom_cache("", build)) return fail(__LINE__);
    const auto configure = [&] { configure_hot_rom(memory.data.data() + 0x60000, 0x80100000, 0x1000, true); };
    configure();
    auto *state = matched_kernel_access::state(*cpu);
    state->Reg[15] = 0x80100000; state->TFlag = 0; state->Cpsr = 16; state->NumInstrsToExecute = 1;
    const auto arm = compile_missing(state);
    state->Reg[15] = 0x80100020; state->TFlag = 1;
    const auto thumb = compile_missing(state);
    if (!arm || !thumb || !request_rom_cache_export(path)) return fail(__LINE__);
    prepare_compiled_code(*cpu);
    if (rom_cache_status() != 2) return fail(__LINE__);
    std::ifstream input(path, std::ios::binary);
    const std::vector<char> saved_bytes{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    const auto write = [&](const std::vector<char> &bytes) {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file.write(bytes.data(), bytes.size());
    };
    const auto clear = [&] {
        for (auto key : {0x80100000u, 0x80100008u, 0x80100021u, 0x80100025u}) {
            const auto function = global_registry().lookup(key);
            global_registry().unregister_function(key);
            if (function) EM_ASM({removeFunction($0);}, reinterpret_cast<std::uintptr_t>(function));
        }
    };
    clear();
    // Fail closed for another build, ROM, policy, truncation or corrupted body.
    for (unsigned variant = 0; variant < 5; ++variant) {
        auto bytes = saved_bytes;
        if (variant == 3) bytes.resize(bytes.size() - 1);
        if (variant == 4) bytes[bytes.size() - 28] ^= 1;
        write(bytes);
        configure_rom_cache(path, variant == 0 ? std::string(64, 'b') : build);
        if (variant == 1) memory.data[0x60800] ^= 1;
        if (variant == 2) memory_experiment::mode = 2;
        configure(); prepare_compiled_code(*cpu);
        if (rom_cache_status() != -1 || global_registry().lookup(0x80100000)) return fail(__LINE__);
        if (variant == 1) memory.data[0x60800] ^= 1;
        memory_experiment::mode = 0;
    }
    // Fingerprinting the whole live ROM also covers every folded ROM callee.
    // A host patch after setup rejects the cache and falls back to compilation.
    write(saved_bytes); configure_rom_cache(path, build); configure();
    memory.data[0x60000] ^= 1; prepare_compiled_code(*cpu);
    if (rom_cache_status() != -1 || global_registry().lookup(0x80100000)
            || global_registry().lookup(0x80100021)) return fail(__LINE__);
    state->Reg[15] = 0x80100000; state->TFlag = 0;
    const auto patched = compile_missing(state);
    if (!patched || execute_single(state, patched) || state->Reg[0] != 6) return fail(__LINE__);
    memory.data[0x60000] ^= 1; clear();
    write(saved_bytes); configure_rom_cache(path, build); configure(); prepare_compiled_code(*cpu);
    if (rom_cache_status() != 2 || compilation.rom_translations != 0) return fail(__LINE__);
    state->Reg[15] = 0x80100000; state->TFlag = 0; state->aot_exit = 0;
    const auto cached_arm = lookup_compiled(state);
    if (!cached_arm || execute_single(state, cached_arm) || state->Reg[0] != 7) return fail(__LINE__);
    state->Reg[15] = 0x80100020; state->TFlag = 1; state->aot_exit = 0;
    const auto cached_thumb = lookup_compiled(state);
    if (!cached_thumb || execute_single(state, cached_thumb) || state->Reg[0] != 9) return fail(__LINE__);
    clear();
    printf(" PASS ROM translation cache: ARM/Thumb round trip, zero retranslations, mismatch/corruption rejection and host-patch fallback\n");
#endif
    return true;
}

static bool test_flat_precompile_entries() {
#ifdef __EMSCRIPTEN__
    struct restore {
        unsigned mode = memory_experiment::mode;
        bool hot = hot_compilation_enabled;
        ~restore() { memory_experiment::mode = mode; hot_compilation_enabled = hot; }
    } saved;
    memory_experiment::mode = 0; hot_compilation_enabled = true;
    test_mem memory;
    r12l1::exclusive_monitor monitor(1);
    auto cpu = make_cpu(memory, monitor);
    cpu->code_address_space = 962;
    memory.write32(0x9e000, 0xe3a00007); memory.write32(0x9e004, 0xef0000ff);
    memory.write32(0x9e100, 0xe3a00008); memory.write32(0x9e104, 0xef0000ff);
    memory.write32(0x9e108, 0xe3a00009); memory.write32(0x9e10c, 0xef0000ff);
    queue_precompile_image(962,0x9e000,memory.data.data()+0x9e000,8,{0x9e000});
    queue_precompile_image(962,0x9e100,memory.data.data()+0x9e100,16,{}, {0x9e100});
    prepare_compiled_code(*cpu);
    auto *state = matched_kernel_access::state(*cpu);
    state->TFlag = 0; state->Cpsr = 16; state->NumInstrsToExecute = 1;
    state->Reg[15] = 0x9e100;
    const auto callback = lookup_compiled(state);
    if (!callback || execute_single(state,callback) || state->Reg[0] != 8 || state->Reg[15] != 0x9e108) return false;
    if (lookup_compiled(state)) return false;
    const auto continuation = compile_missing(state);
    if (!continuation || execute_single(state,continuation) || state->Reg[0] != 9) return false;
    if (compilation.rom_translation_us.load() + compilation.ram_translation_us.load()
            != compilation.translation_us.load() || compilation.ram_translations.load() < 3) return false;
    invalidate_ram_code(0x9e000,0x110); retire_process_code(962); prepare_compiled_code(*cpu);
    printf(" PASS flat callback preparation: entry compiled, unused continuation deferred, demand execution preserved\n");
#endif
    return true;
}
