#include <cpu/aot/code_cache.h>
#include <cpu/aot/exit_census.h>
#ifdef __wasm_simd128__
#include <wasm_simd128.h>
#endif

namespace eka2l1::arm::aot {
    unsigned code_compare_mode = 0;

    validated_code_cache::block *validated_code_cache::find_original(std::uint32_t pc_mode, core &cpu) {
        return find_original_impl<false>(pc_mode, cpu);
    }
    validated_code_cache::block *validated_code_cache::find_trusted_original(std::uint32_t pc_mode, core &cpu) {
        return find_original_impl<true>(pc_mode, cpu);
    }
    aot_func validated_code_cache::lookup_trusted_uncached(std::uint32_t pc_mode, core &cpu) {
        auto *entry = find_trusted_original(pc_mode, cpu);
        if (!entry) return nullptr;
        if (entry->function && entry->mapping_generation)
            dispatch_[recent_index(entry->key)] = {entry->key, entry->mapping_generation,
                entry->mapping_source, entry->function};
        return entry->function;
    }
    template<bool TrustBytes>
    validated_code_cache::block *validated_code_cache::find_original_impl(std::uint32_t pc_mode, core &cpu) {
        const auto generation = cpu.code_mapping_generation
            ? cpu.code_mapping_generation->load(std::memory_order_acquire) : 0;
        const auto k = key(cpu.code_address_space, pc_mode);
        // Any general validation supersedes a cached dispatch identity, even
        // when refresh fails or the generation source temporarily reports zero.
        forget_dispatch(k);
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
            if (TrustBytes || bytes_match(*recent)) return recent;
            recent->live = false;
            current_.erase(k);
            recent = nullptr;
            ++invalidations;
            if(exit_census::counting())++exit_census::invalidations["exact_bytes"];
            return nullptr;
        }
        core::code_mapping view;
        if (!cpu.resolve_code || !cpu.resolve_code(pc_mode & ~1u, view)) {
            // No cached pointer may survive a failed mapping refresh.
            if (recent && recent->key == k) recent->mapping_generation = 0;
            return nullptr;
        }
        auto *entry = find_mapped<TrustBytes>(pc_mode, view, &cpu);
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
