#include <cpu/aot/code_cache.h>
#ifdef __wasm_simd128__
#include <wasm_simd128.h>
#endif

namespace eka2l1::arm::aot {
    unsigned code_compare_mode = 0;
    bool code_lookup_outline = false;

    void validated_code_cache::reject_recent(std::uint64_t k, block &entry) {
        entry.live = false;
        current_.erase(k);
        recent_[recent_index(k)] = nullptr;
        ++invalidations;
    }

    validated_code_cache::block *validated_code_cache::find_original(std::uint32_t pc_mode, core &cpu) {
        const auto generation = cpu.code_mapping_generation
            ? cpu.code_mapping_generation->load(std::memory_order_acquire) : 0;
        const auto k = key(cpu.code_address_space, pc_mode);
        auto &recent = recent_[recent_index(k)];
        // A recent-slot collision does not invalidate an entry's mapping.
        // Recover the stable version first, then use the same generation
        // guard as a recent hit. Exact bytes are still checked below.
        if (!recent || !recent->live || recent->key != k) {
            auto it = current_.find(k);
            if (it == current_.end()) return nullptr;
            recent = &versions_[it->second];
        }
        if (generation && recent->mapping_source == cpu.code_mapping_generation
            && recent->mapping_generation == generation) {
            if (bytes_match(*recent, false)) return recent;
            recent->live = false;
            current_.erase(k);
            recent = nullptr;
            ++invalidations;
            return nullptr;
        }
        core::code_mapping view;
        if (!cpu.resolve_code || !cpu.resolve_code(pc_mode & ~1u, view)) {
            // No cached pointer may survive a failed mapping refresh.
            if (recent && recent->key == k) recent->mapping_generation = 0;
            return nullptr;
        }
        auto *entry = find(pc_mode, view, &cpu);
        if (entry) {
            entry->mapping_generation = generation;
            entry->mapping_source = cpu.code_mapping_generation;
        }
        return entry;
    }

    bool equal_code_bytes(const std::uint8_t *a, const std::uint8_t *b, std::size_t size) {
#ifdef __wasm_simd128__
        if (code_compare_mode == 2) {
            // All eight loads remain in the original spans. Combine mismatch
            // bits before the branch; unequal bytes can never cancel via OR.
            while (size >= 64) {
                const auto d0 = wasm_v128_xor(wasm_v128_load(a), wasm_v128_load(b));
                const auto d1 = wasm_v128_xor(wasm_v128_load(a + 16), wasm_v128_load(b + 16));
                const auto d2 = wasm_v128_xor(wasm_v128_load(a + 32), wasm_v128_load(b + 32));
                const auto d3 = wasm_v128_xor(wasm_v128_load(a + 48), wasm_v128_load(b + 48));
                if (wasm_v128_any_true(wasm_v128_or(wasm_v128_or(d0, d1), wasm_v128_or(d2, d3)))) return false;
                a += 64; b += 64; size -= 64;
            }
        }
        if (code_compare_mode == 1 && size >= 16) {
            // Compare the last full vector instead of decomposing a short tail.
            // Both loads stay inside the original span, even for size 17. The
            // overlap repeats comparisons only; every byte is still checked.
            const auto *end_a = a + size - 16;
            const auto *end_b = b + size - 16;
            while (size > 16) {
                if (wasm_v128_any_true(wasm_v128_xor(wasm_v128_load(a), wasm_v128_load(b)))) return false;
                a += 16; b += 16; size -= 16;
            }
            return !wasm_v128_any_true(wasm_v128_xor(wasm_v128_load(end_a), wasm_v128_load(end_b)));
        }
        while (size >= 16) {
            if (wasm_v128_any_true(wasm_v128_xor(wasm_v128_load(a), wasm_v128_load(b)))) return false;
            a += 16; b += 16; size -= 16;
        }
        // At most 15 bytes remain. Constant-width memcpy loads permit unaligned
        // access without overreading and avoid a libc call for every block.
        if (size >= 8) {
            std::uint64_t x, y; std::memcpy(&x,a,8); std::memcpy(&y,b,8);
            if (x != y) return false;
            a += 8; b += 8; size -= 8;
        }
        if (size >= 4) {
            std::uint32_t x, y; std::memcpy(&x,a,4); std::memcpy(&y,b,4);
            if (x != y) return false;
            a += 4; b += 4; size -= 4;
        }
        if (size >= 2) {
            std::uint16_t x, y; std::memcpy(&x,a,2); std::memcpy(&y,b,2);
            if (x != y) return false;
            a += 2; b += 2; size -= 2;
        }
        return !size || *a == *b;
#else
        return std::memcmp(a,b,size) == 0;
#endif
    }
}
