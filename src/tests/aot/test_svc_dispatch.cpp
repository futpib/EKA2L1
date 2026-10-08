#include <kernel/svc_handler.h>
#include <kernel/svc_registry.h>

#include <array>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <vector>

using namespace eka2l1::hle;

static unsigned checks;
static void require(bool condition, const char *message) {
    ++checks;
    if (!condition) { std::cerr << "FAIL " << message << '\n'; std::exit(1); }
}

static epoc_import_func binding(unsigned value, unsigned &observed) {
    return {[value, &observed](auto *, auto *, auto *) { observed = value; }, "fixture"};
}

static void test_registry() {
    svc_registry registry;
    func_map reference;
    unsigned observed = 0;
    const std::uint32_t ordinals[] = {0, 5, 0x4d, 0xdc, 0xff, 256, 0x800000,
        0x800005, 0x8000ff, 0x800100, 0xc10000, 0xc10001, 0xffffffff};
    for (auto ordinal : ordinals) reference.emplace(ordinal, binding(ordinal, observed));
    registry.insert(reference.begin(), reference.end());
    // Later version registrations must not replace an earlier 9.1 override.
    func_map duplicate{{0x4d, binding(999, observed)}};
    registry.insert(duplicate.begin(), duplicate.end());
    const auto *stable = registry.find(0x800005);
    func_map growth;
    for (unsigned i = 0; i < 20000; ++i) growth.emplace(0x10000000u + i, binding(i, observed));
    registry.insert(growth.begin(), growth.end());
    reference.insert(growth.begin(), growth.end());
    require(registry.find(0x800005) == stable, "dense pointer survived map rehash");
    for (const auto &entry : reference) {
        const auto *found = registry.find(entry.first);
        require(found != nullptr, "registered binding present");
        observed = 0;
        found->func(nullptr, nullptr, nullptr);
        const auto actual = observed;
        entry.second.func(nullptr, nullptr, nullptr);
        require(actual == observed, "lookup agrees with original map");
    }
    for (unsigned ordinal : {1u, 255u, 256u, 0x800001u, 0x8000ffu, 0x800100u,
            0x7fffffu, 0x810000u, 0xffffff00u}) {
        require(bool(registry.find(ordinal)) == bool(reference.count(ordinal)), "no namespace aliases");
    }
    for (auto ordinal : ordinals) {
        registry.erase(ordinal);
        require(!registry.find(ordinal), "erase invalidates dense and rare entries");
    }
    registry.clear();
    for (const auto &entry : reference) require(!registry.find(entry.first), "clear invalidates all entries");

    // A callable may erase itself, replace registrations and invoke the new entry.
    // The in-flight snapshot must own its captures until it returns.
    auto owner = std::make_shared<unsigned>(23);
    std::weak_ptr<unsigned> lifetime = owner;
    epoc_import_func callback{[&, owner](auto *, auto *, auto *) {
        registry.clear();
        func_map replacement{{5, binding(71, observed)}};
        registry.insert(replacement.begin(), replacement.end());
        auto nested = registry.find(5)->func;
        nested(nullptr, nullptr, nullptr);
        require(observed == 71 && *owner == 23, "reentrant registration and live capture");
    }, "self replacing"};
    func_map callbacks{{5, callback}};
    registry.insert(callbacks.begin(), callbacks.end());
    owner.reset(); callback = {}; callbacks.clear();
    {
        auto snapshot = registry.find(5)->func;
        snapshot(nullptr, nullptr, nullptr);
        require(!lifetime.expired(), "snapshot owns erased callback");
    }
    require(lifetime.expired(), "callback capture released after invocation");
}

struct fake_cpu {
    std::array<std::uint32_t, 16> regs{};
    std::uint32_t cpsr{};
    std::vector<unsigned> actions;
    std::uint32_t get_pc() { actions.push_back(1); return regs[15]; }
    std::uint32_t get_reg(unsigned reg) { require(reg == 12, "only 9.1 r12 is read"); actions.push_back(2); return regs[reg]; }
    std::uint32_t get_lr() { actions.push_back(3); return regs[14]; }
    std::uint32_t get_cpsr() { actions.push_back(4); return cpsr; }
    void set_pc(std::uint32_t value) { actions.push_back(5); regs[15] = value; }
    void set_cpsr(std::uint32_t value) { actions.push_back(6); cpsr = value; }
};

template<svc_return_convention Convention>
static void test_convention() {
    for (unsigned flags = 0; flags < 64; ++flags)
    for (bool rom : {false, true}) for (unsigned ordinal : {5u, 0xffu, 0x800000u})
    for (unsigned target : {0x2000u, 0x2001u}) {
        fake_cpu cpu;
        for (unsigned i = 0; i < 16; ++i) cpu.regs[i] = 0x56780000u + i;
        cpu.regs[12] = target; cpu.regs[15] = rom ? 0x80123458 : 0x70001234;
        cpu.cpsr = (flags << 26) | ((flags & 1) << 5) | 16;
        const auto initial = cpu;
        const bool pre_return = Convention == svc_return_convention::epoc91 && ordinal != 0xff && rom;
        invoke_svc<Convention>(cpu, ordinal, [&](unsigned actual) {
            cpu.actions.push_back(7);
            require(actual == ordinal, "ordinal passed without normalization");
            require(cpu.regs[15] == (pre_return ? target & ~1u : initial.regs[15]), "PC visible to dispatch");
            const auto pre_flags = (initial.cpsr & ~0x20u) | ((target & 1) << 5);
            require(cpu.cpsr == (pre_return ? pre_flags : initial.cpsr), "flags visible to dispatch");
            // EKA1 must use the handler's modified LR and flags, not entry values.
            cpu.regs[14] = target ^ 0x4001; cpu.regs[15] = 0x34567890; cpu.cpsr = 0xa00000f0;
        }, [&](unsigned pc) { cpu.actions.push_back(8); require(pc == initial.regs[15], "ROM predicate input"); return rom; });
        const bool post_return = Convention == svc_return_convention::eka1;
        require(cpu.regs[15] == (post_return ? (target ^ 0x4001) & ~1u : 0x34567890), "final return PC");
        require(cpu.cpsr == (post_return ? (0xa00000f0u & ~0x20u) | (((target ^ 0x4001) & 1) << 5) : 0xa00000f0u), "final return flags");
        for (unsigned i = 0; i < 14; ++i) require(cpu.regs[i] == initial.regs[i], "unrelated registers untouched");
        std::vector<unsigned> expected;
        if (Convention == svc_return_convention::epoc91 && ordinal != 0xff) {
            expected = {1, 8};
            if (rom) expected.insert(expected.end(), {2, 4, 5, 6});
        }
        expected.push_back(7);
        if (post_return) expected.insert(expected.end(), {3, 4, 5, 6});
        require(cpu.actions == expected, "return convention access order");
    }
}

int main() {
    test_registry();
    test_convention<svc_return_convention::direct>();
    test_convention<svc_return_convention::eka1>();
    test_convention<svc_return_convention::epoc91>();
    std::cout << "PASS " << checks << " registry lifetime and SVC return checks\n";
}
