#pragma once

#include <array>
#include <cstdint>
#include <initializer_list>

namespace eka2l1::arm::aot::wasm_cost {
    enum class kind { structural, local, constant, load, store, control, call,
        global, memory_size, arithmetic, division, unknown, count };

    constexpr kind classify(std::uint8_t opcode) {
#define WASM_COST(first, last, category, immediate) \
        if (opcode >= first && opcode <= last) return kind::category;
#include <cpu/aot/wasm_cost_model.def>
#undef WASM_COST
        return kind::unknown;
    }

    // Signed components also represent candidate-minus-baseline path deltas.
    // These count WASM operations, not predicted native instructions or cycles.
    struct cost {
        std::array<std::int64_t, static_cast<unsigned>(kind::count)> counts{};
        bool supported = true;
        constexpr std::int64_t operator[](kind category) const {
            return counts[static_cast<unsigned>(category)];
        }
        constexpr cost &add(std::uint8_t opcode, std::int64_t repetitions = 1) {
            const auto category = classify(opcode);
            supported &= category != kind::unknown;
            counts[static_cast<unsigned>(category)] += repetitions;
            return *this;
        }
        constexpr std::int64_t operations() const {
            std::int64_t result = 0;
            for (unsigned i = 1; i < static_cast<unsigned>(kind::unknown); ++i) result += counts[i];
            return result;
        }
        constexpr bool known() const { return supported; }
        constexpr cost &operator+=(const cost &other) {
            supported &= other.supported;
            for (unsigned i = 0; i < counts.size(); ++i) counts[i] += other.counts[i];
            return *this;
        }
        constexpr cost operator*(std::int64_t n) const {
            cost result = *this;
            for (auto &value : result.counts) value *= n;
            return result;
        }
    };
    constexpr cost operator+(cost a, const cost &b) { return a += b; }
    constexpr cost operator-(cost a, const cost &b) { return a += b * -1; }
    constexpr cost sequence(std::initializer_list<std::uint8_t> opcodes) {
        cost result;
        for (auto opcode : opcodes) result.add(opcode);
        return result;
    }

    // The caller supplies corresponding paths and proves their coverage and
    // equal guest/helper semantics. This is not a control-flow equivalence
    // prover. Omitting a fallback or an external dispatcher is not made sound
    // by this ledger: its reported scope is generated WASM only.
    struct path_gate {
        unsigned paths = 0;
        bool known = true, non_growing = true, reduced = false;
        bool material_non_growing = true;
        std::int64_t worst_operations = INT64_MIN;
        cost worst_components{};

        void add_delta(const cost &delta, bool must_reduce = false) {
            ++paths;
            known &= delta.known();
            const auto n = delta.operations();
            non_growing &= n <= 0 && (!must_reduce || n < 0);
            reduced |= n < 0;
            if (n > worst_operations) worst_operations = n;
            for (unsigned i = 1; i < static_cast<unsigned>(kind::unknown); ++i) {
                if (delta.counts[i] > worst_components.counts[i]) worst_components.counts[i] = delta.counts[i];
                // Local moves/constants may disappear in the native compiler.
                // Their removal cannot pay for new loads, branches or calls.
                if (i != static_cast<unsigned>(kind::local) && i != static_cast<unsigned>(kind::constant))
                    material_non_growing &= delta.counts[i] <= 0;
            }
        }
        void compare(const cost &before, const cost &after, bool must_reduce = false) {
            known &= before.known() && after.known();
            add_delta(after - before, must_reduce);
        }
        bool wasm_improves() const { return paths && known && non_growing && reduced; }
        bool no_added_material_work() const { return wasm_improves() && material_non_growing; }
    };

    // Compare a fixed setup with repeated removed work without multiplying
    // input-dependent counts into a cost vector or risking integer overflow.
    constexpr bool amortizes(unsigned uses, const cost &per_use, const cost &once) {
        return per_use.known() && once.known() && per_use.operations() > 0
            && once.operations() >= 0
            && uses > static_cast<std::uint64_t>(once.operations() / per_use.operations());
    }

    // Reusing an existing successful proof replaces N get/if pairs with one.
    // This accounts only for the removed repeated tests, not for establishing
    // a new proof. An optimization adding a guard must include that separately.
    constexpr bool repeated_test_saves_work(unsigned uses) {
        constexpr auto test = sequence({0x20, 0x04});
        return amortizes(uses, test, test);
    }
}
