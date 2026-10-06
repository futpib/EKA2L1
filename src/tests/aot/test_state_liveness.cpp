#include <cpu/aot/state_locals.h>
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdio>
#include <functional>

using namespace eka2l1::arm::aot;

namespace {
    struct fixture {
        wasm_func_def function{"probe", {}, 1};
        state_local_cache cache;
        fixture() { cache.enabled = true; cache.first_local = 2; }
        void op(std::uint8_t code) { function.body.push_back(code); }
        void imm(unsigned n) { state_local_cache::leb(function.body, n); }
        void get(unsigned n) { op(op_local_get); imm(n); }
        void set(unsigned n) { op(op_local_set); imm(n); }
        void constant(unsigned n) { op(op_i32_const); imm(n); }
        void raw(unsigned offset) { get(0); op(op_i32_load); imm(2); imm(offset); }
        void load(unsigned offset) { get(cache.local(offset)); }
        void store(unsigned offset) { set(cache.local(offset)); cache.written.insert(offset); }
        void write(unsigned offset, unsigned n) { constant(n); store(offset); }
        void control(std::uint8_t code) { op(code); op(type_void); }
        void branch(std::uint8_t code, unsigned depth) { op(code); imm(depth); }
        void flush() { cache.barrier_at(function.body.size()); }
        void helper() {
            get(0); flush(); op(op_call); imm(0);
            cache.barrier_at(function.body.size(), true);
        }
        void result() { load(0); flush(); op(op_return); }
        void fixed_result() { constant(3); flush(); op(op_return); }
    };
    void hex(const std::vector<std::uint8_t> &bytes) { for (auto byte : bytes) std::printf("%02x", byte); }
}

int main(int argc, char **) {
    using test = std::pair<const char *, std::function<void(fixture &)>>;
    const std::vector<test> tests{
        {"overwrite", [](auto &f) { f.write(0, 7); f.fixed_result(); }},
        {"early_exit", [](auto &f) {
            f.raw(8); f.control(op_if); f.fixed_result(); f.op(op_end);
            f.write(0, 7); f.fixed_result();
        }},
        {"conditional_write", [](auto &f) {
            f.raw(8); f.control(op_if); f.write(0, 7); f.op(op_end); f.fixed_result();
        }},
        {"both_arms_write", [](auto &f) {
            f.raw(8); f.control(op_if); f.write(0, 7); f.op(op_else); f.write(0, 9); f.op(op_end); f.result();
        }},
        {"shared_exit", [](auto &f) {
            f.cache.shared_return = true;
            f.raw(8); f.control(op_if); f.write(0, 7); f.constant(3); f.branch(op_br, 1); f.op(op_end);
            f.write(0, 9); f.constant(3); f.branch(op_br, 0);
        }},
        {"shared_early_exit", [](auto &f) {
            f.cache.shared_return = true;
            f.raw(8); f.control(op_if); f.constant(3); f.branch(op_br, 1); f.op(op_end);
            f.write(0, 9); f.constant(3); f.branch(op_br, 0);
        }},
        {"loop_read", [](auto &f) {
            f.raw(8); f.set(1); f.control(op_block); f.control(op_loop);
            f.get(1); f.op(op_i32_eqz); f.branch(op_br_if, 1);
            f.load(0); f.constant(1); f.op(op_i32_add); f.store(0);
            f.get(1); f.constant(1); f.op(op_i32_sub); f.set(1); f.branch(op_br, 0);
            f.op(op_end); f.op(op_end); f.result();
        }},
        {"flush_then_read", [](auto &f) {
            f.load(0); f.constant(1); f.op(op_i32_add); f.store(0); f.flush();
            f.load(0); f.store(4); f.fixed_result();
        }},
        {"clean_flush_then_read", [](auto &f) {
            f.flush(); f.load(0); f.constant(1); f.op(op_i32_add); f.store(0); f.result();
        }},
        {"helper_future_read", [](auto &f) {
            f.write(0, 7); f.helper(); f.load(4); f.store(0); f.result();
        }},
        {"helper_overwrite", [](auto &f) { f.write(0, 7); f.helper(); f.write(0, 9); f.result(); }},
        {"helper_twice", [](auto &f) { f.write(0, 7); f.helper(); f.helper(); f.fixed_result(); }},
        {"conditional_helper", [](auto &f) {
            f.raw(8); f.control(op_if); f.helper(); f.op(op_end); f.result();
        }},
        {"bare_call_clobber", [](auto &f) {
            f.raw(8); f.control(op_if); f.write(0, 7); f.op(op_end);
            f.get(0); f.op(op_call); f.imm(0); f.fixed_result();
        }},
        {"branch_table", [](auto &f) {
            f.control(op_block); f.control(op_block); f.raw(8);
            f.op(op_br_table); f.imm(1); f.imm(0); f.imm(1);
            f.op(op_end); f.write(0, 7); f.op(op_end); f.result();
        }},
        {"loop_helper", [](auto &f) {
            f.raw(8); f.set(1); f.control(op_block); f.control(op_loop);
            f.get(1); f.op(op_i32_eqz); f.branch(op_br_if, 1);
            f.load(0); f.constant(1); f.op(op_i32_add); f.store(0); f.helper();
            f.get(1); f.constant(1); f.op(op_i32_sub); f.set(1); f.branch(op_br, 0);
            f.op(op_end); f.op(op_end); f.result();
        }},
        {"reload_suffix", [](auto &f) {
            const auto slot = f.cache.local(4);
            f.cache.reload_suffix = {op_local_get, static_cast<std::uint8_t>(slot), op_local_set, 1};
            f.helper(); f.get(1); f.store(0); f.result();
        }},
        {"numeric_immediates", [](auto &f) {
            f.op(op_i64_const); f.imm(999999); f.op(op_drop);
            f.op(op_f32_const); for (unsigned n = 0; n < 4; ++n) f.op(0); f.op(op_drop);
            f.op(op_f64_const); for (unsigned n = 0; n < 8; ++n) f.op(0); f.op(op_drop);
            f.write(0, 7); f.fixed_result();
        }}
    };
    unsigned changed = 0;
    for (const auto &[name, emit] : tests) {
        fixture f; emit(f);
        auto old = f.function, current = f.function;
        f.cache.finish(old, false); f.cache.finish(current);
        assert(current.body.size() <= old.body.size());
        changed += current.body != old.body;
        if (argc > 1) {
            std::printf("STATE_PROBE {\"name\":\"%s\",\"locals\":%u,\"before\":\"", name, old.num_locals);
            hex(old.body); std::printf("\",\"after\":\""); hex(current.body); std::puts("\"}");
        }
    }
    assert(changed >= 8);

    // Unknown instructions and unsupported layouts preserve exact bytes.
    for (unsigned kind = 0; kind < 3; ++kind) {
        fixture f; f.write(0, 7); f.fixed_result();
        if (kind == 0) f.op(0xfd);
        if (kind == 1) { f.op(op_i32_const); f.op(0x80); }
        if (kind == 2) for (unsigned n = 1; n <= 64; ++n) f.cache.local(n * 4);
        auto old = f.function, current = f.function;
        f.cache.finish(old, false); f.cache.finish(current);
        assert(old.body == current.body);
    }
    // Both forms of outlined operand must move with removed entry transfers.
    fixture f; f.write(0, 7); f.get(0); f.op(op_call);
    const auto operand = f.function.body.size();
    f.function.body.insert(f.function.body.end(), {0x80, 0x80, 0x80, 0x80, 0});
    f.fixed_result();
    f.function.outlined_calls.push_back({std::make_shared<wasm_func_def>(), static_cast<unsigned>(operand)});
    auto old = f.function, current = f.function;
    f.cache.finish(old, false);
    current.outlined_callee = std::make_shared<wasm_func_def>();
    current.outlined_call_offset = old.outlined_calls[0].call_offset;
    f.cache.finish(current);
    assert(current.outlined_calls[0].call_offset < old.outlined_calls[0].call_offset);
    assert(current.outlined_call_offset == current.outlined_calls[0].call_offset);
    assert(current.body[current.outlined_call_offset - 1] == op_call);
    std::printf("PASS state liveness: %zu CFG fixtures, %u shrink; fallbacks and both call relocations\n", tests.size(), changed);
}
