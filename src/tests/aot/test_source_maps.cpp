#include <cpu/aot/state_locals.h>
#include <cpu/aot/wasm_emitter.h>
#include <cassert>
#include <fstream>
#include <iostream>

using namespace eka2l1::arm::aot;

int main(int argc, char **argv) {
    assert(argc == 2);
    wasm_func_def function;
    function.export_name = "f_1879048192";
    function.num_locals = 0;
    function.body = {op_local_get, 0, op_i32_load, 2, 0, op_return};
    source_emission emission{&function.sources, 0x70000000, source_kind::guest_memory};
    emission.mark(0);
    emission.kind = source_kind::dispatch; emission.mark(5);
    auto module = build_wasm_module({function});
    assert(!module.empty());
    std::ofstream(argv[1], std::ios::binary).write(reinterpret_cast<const char *>(module.data()), module.size());

    source_marks marks{{0, 10, source_kind::guest}, {4, 20, source_kind::guest_memory},
        {9, 30, source_kind::state}}, copied;
    copy_source_marks(marks, 2, 7, 0, copied);
    assert(copied.size() == 2 && copied[0].offset == 0 && copied[0].pc == 10);
    assert(copied[1].offset == 2 && copied[1].pc == 20);
    copy_source_marks(marks, 9, 12, 5, copied);
    assert(copied.back().offset == 5 && copied.back().pc == 30);

    wasm_func_def cached;
    cached.num_locals = 1;
    state_local_cache cache;
    cache.enabled = true; cache.first_local = 2;
    assert(cache.local(0) == 2);
    cache.written.insert(0);
    cached.body = {op_i32_const, 7, op_local_set, 2, op_i32_const, 1, op_return};
    cached.sources = {{0, 0x1234, source_kind::guest}};
    cache.barrier_at(4);
    cache.finish(cached);
    assert(!cached.sources.empty());
    for (std::size_t n = 0; n < cached.sources.size(); ++n) {
        assert(cached.sources[n].offset < cached.body.size());
        if (n) assert(cached.sources[n-1].offset <= cached.sources[n].offset);
    }
    bool guest = false, state = false;
    for (const auto &mark : cached.sources) {
        guest |= mark.pc == 0x1234;
        state |= mark.kind == source_kind::state;
    }
    assert(guest && state);
    std::cout << "PASS: source relocation, cache pruning and module metadata\n";
}
