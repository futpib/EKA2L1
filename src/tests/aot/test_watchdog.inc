#include <cpu/aot/watchdog.h>

#ifdef __EMSCRIPTEN__
EM_JS(void, watchdog_test_arm, (unsigned address, unsigned handshake), {
    const Worker = require('node:worker_threads').Worker;
    new Worker(`
      const {workerData: d} = require('node:worker_threads');
      const flag = new Int32Array(d.memory, d.address, 1);
      const handshake = new Int32Array(d.memory, d.handshake, 1);
      Atomics.store(handshake, 0, 1); Atomics.notify(handshake, 0);
      Atomics.wait(handshake, 0, 1);
      Atomics.wait(new Int32Array(new SharedArrayBuffer(4)), 0, 0, 10);
      Atomics.store(flag, 0, 1);
    `, {eval: true, workerData: {memory: wasmMemory.buffer, address, handshake}});
    const ready = new Int32Array(wasmMemory.buffer, handshake, 1);
    while (!Atomics.load(ready, 0)) Atomics.wait(ready, 0, 0, 1000);
});
EM_JS(int, watchdog_test_run, (const std::uint8_t *bytes, unsigned size,
        unsigned state, unsigned handshake), {
    const module = new WebAssembly.Module(new Uint8Array(wasmMemory.buffer, bytes, size));
    const instance = new WebAssembly.Instance(module, {env: {memory: wasmMemory,
      tlb_read32: Module._test_tlb_read32, tlb_write32: Module._test_tlb_write32,
      tlb_read8: Module._test_tlb_read8, tlb_write8: Module._test_tlb_write8,
      tlb_read16: Module._test_tlb_read16, tlb_write16: Module._test_tlb_write16}});
    const ready = new Int32Array(wasmMemory.buffer, handshake, 1);
    Atomics.store(ready, 0, 2); Atomics.notify(ready, 0);
    return Object.values(instance.exports).find(f => typeof f === 'function')(state);
});
#endif

static bool test_watchdog() {
#ifdef __EMSCRIPTEN__
    struct restore {
        ~restore() { watchdog::request = 0; }
    } saved;
    const auto compile = [](const std::vector<std::uint32_t> &words) {
        return translate_arm_block(reinterpret_cast<const std::uint8_t *>(words.data()),
            words.size() * 4, 0x1000, nullptr, nullptr, true, false, true, true,
            nullptr, false, arm_ir_policy::write_spans);
    };
    const auto module = [](const translate_result &tr) {
        return build_wasm_module({tr.func}, {{"env","tlb_read32",2,true},
            {"env","tlb_write32",3,false},{"env","tlb_read8",2,true},
            {"env","tlb_write8",3,false},{"env","tlb_read16",2,true},
            {"env","tlb_write16",3,false}});
    };
    const auto initial = [](std::uint32_t *state) {
        std::fill(state, state + 256, 0);
        state[14] = 0x2000; state[15] = 0x1000;
        state[state_offsets::CPSR/4] = state[state_offsets::MODE/4] = 16;
        state[state_offsets::NUM_INSTRS_TO_EXECUTE/4] = UINT32_MAX;
        state[state_offsets::NIRQ/4] = 1;
    };
    alignas(8) std::uint32_t state[256];
    const auto finite = compile({0xe2811001, 0xe2500001, 0x1afffffc, 0xe12fff1e});
    if (finite.proved_terminating_loops != 1 || finite.watchdog_safepoints) return false;
    const auto finite_module = module(finite);
    for (auto n : {1u, 2u, 31u, 1000000u}) {
        initial(state); state[0] = n;
        // An already expired watchdog cannot stop this proved finite loop.
        watchdog::request = 1;
        const auto count = js_run_aot_wasm(finite_module.data(), finite_module.size(),
            reinterpret_cast<std::uint8_t *>(state), sizeof(state));
        if (count != 1 || state[0] || state[1] != n || state[15] != 0x2000
                || state[state_offsets::NFLAG/4] || !state[state_offsets::ZFLAG/4]
                || !state[state_offsets::CFLAG/4] || state[state_offsets::VFLAG/4]) {
            printf("FAIL finite watchdog n=%u count=%d r0=%u r1=%u pc=%x\n",n,count,state[0],state[1],state[15]);
            return false;
        }
    }
    const std::uint8_t thumb[] = {0x01,0x30,0x02,0x31,0x70,0x47};
    auto thumb_tr = translate_thumb_block(thumb, sizeof(thumb), 0x1000,
        nullptr, nullptr, true, false, true);
    auto thumb_module = module(thumb_tr);
    initial(state); state[state_offsets::TFLAG/4] = 1; state[14] = 0x2001;
    if (js_run_aot_wasm(thumb_module.data(),thumb_module.size(),
            reinterpret_cast<std::uint8_t *>(state),sizeof(state)) != 1
            || state[0] != 1 || state[1] != 2 || state[15] != 0x2000) return false;

    // Zero is also the pending-SVC sentinel. A progress status must never let
    // the chain run the return instruction and discard the pending syscall.
    const std::uint8_t svc[] = {0x80,0xdf,0x70,0x47};
    auto svc_tr = translate_thumb_block(svc,sizeof(svc),0x1000,
        nullptr,nullptr,true,false,true);
    auto svc_module = module(svc_tr);
    initial(state); state[state_offsets::TFLAG/4] = 1;
    if (js_run_aot_wasm(svc_module.data(),svc_module.size(),
            reinterpret_cast<std::uint8_t *>(state),sizeof(state)) != 0
            || state[15] != 0x1002
            || state[state_offsets::AOT_EXIT/4] != (svc_pending | svc_taken | 0x80)) return false;

    // A counter write that invalidates the ranking argument must retain a poll.
    const auto reset = compile({0xe3a00002,0xe2500001,0x1afffffc});
    if (reset.proved_terminating_loops || reset.watchdog_safepoints != 1) return false;
    const auto infinite = compile({0xe2811001,0xeafffffd});
    if (infinite.watchdog_safepoints != 1 || infinite.proved_terminating_loops) return false;
    const auto infinite_module = module(infinite);
    initial(state); watchdog::request = 1;
    if (js_run_aot_wasm(infinite_module.data(),infinite_module.size(),
            reinterpret_cast<std::uint8_t *>(state),sizeof(state)) != 1 || state[1]) return false;
    initial(state); watchdog::request = 0;
    std::atomic<std::uint32_t> handshake{0};
    watchdog_test_arm(reinterpret_cast<std::uintptr_t>(&watchdog::request),
        reinterpret_cast<std::uintptr_t>(&handshake));
    const auto count = watchdog_test_run(infinite_module.data(), infinite_module.size(),
        reinterpret_cast<std::uintptr_t>(state), reinterpret_cast<std::uintptr_t>(&handshake));
    if (count != 1 || !state[1] || state[15] != 0x1000
            || !watchdog::requested()) return false;
    printf("PASS watchdog: finite ARM/Thumb, static loop proof, proof rejection, external atomic interrupt (%d progress status)\n",count);
#endif
    return true;
}
