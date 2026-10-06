#include <cpu/aot/wasm_cost.h>
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdio>

using namespace eka2l1::arm::aot::wasm_cost;

int main() {
    constexpr auto load = sequence({0x20, 0x28, 0x21});
    constexpr auto store = sequence({0x20, 0x20, 0x36});
    static_assert(load.operations() == 3 && load[kind::load] == 1);
    static_assert(store.operations() == 3 && store[kind::store] == 1);
    static_assert(sequence({0x02, 0x03, 0x05, 0x0b}).operations() == 0);
    for (unsigned n = 0; n <= 16; ++n) assert(repeated_test_saves_work(n) == (n > 1));
    assert(!amortizes(2, load, load * 2));
    assert(amortizes(3, load, load * 2));
    assert(!amortizes(100, {}, load));

    path_gate shared;
    shared.compare(load + store, load, true);
    shared.compare({}, {}); // original cold path retained
    assert(shared.wasm_improves() && shared.no_added_material_work());
    assert(shared.material_improves());
    assert(shared.worst_operations == 0);

    path_gate cold_growth;
    cold_growth.compare(load * 8, load, true);
    cold_growth.compare({}, sequence({0x04}));
    assert(!cold_growth.wasm_improves());

    // Raw operation savings do not authorize paying for a new branch with
    // local assignments the native compiler may already have eliminated.
    path_gate dead_locals;
    dead_locals.compare(sequence({0x41, 0x21, 0x41, 0x21}), sequence({0x20, 0x04}), true);
    assert(dead_locals.wasm_improves() && !dead_locals.no_added_material_work());
    assert(dead_locals.worst_components[kind::control] == 1);

    path_gate only_moves;
    only_moves.compare(sequence({0x20, 0x21, 0x41, 0x21}), {});
    assert(only_moves.no_added_material_work() && !only_moves.material_improves());

    path_gate unknown;
    const auto unsupported = sequence({0xfd});
    unknown.compare(unsupported + load, unsupported);
    assert(!unknown.wasm_improves());
    path_gate cancelled_unknown;
    cancelled_unknown.add_delta(unsupported - unsupported - load);
    assert(!cancelled_unknown.wasm_improves());
    path_gate empty;
    assert(!empty.wasm_improves());
    path_gate equal;
    equal.compare(load, load, true);
    assert(!equal.wasm_improves());

    std::puts("PASS shared WASM costs: static paths, cold exits, material work, unknown opcodes");
}
