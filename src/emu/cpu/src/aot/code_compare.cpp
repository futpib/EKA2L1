#include <cpu/aot/code_cache.h>
#include <cpu/aot/exit_census.h>
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
        if(exit_census::counting())++exit_census::invalidations["exact_bytes"];
    }

    validated_code_cache::block *validated_code_cache::find_original(std::uint32_t pc_mode, core &cpu) {
        return find_original_impl<false>(pc_mode, cpu);
    }
    validated_code_cache::block *validated_code_cache::find_trusted_original(std::uint32_t pc_mode, core &cpu) {
        return find_original_impl<true>(pc_mode, cpu);
    }
    template<bool TrustBytes>
    validated_code_cache::block *validated_code_cache::find_original_impl(std::uint32_t pc_mode, core &cpu) {
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
            if (TrustBytes || bytes_match(*recent, false)) return recent;
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

#ifdef __wasm_simd128__
    // Fixed-size two-buffer lowering. Keep the real snapshot in memory; unlike
    // embedded constants this requires neither per-block modules nor recompiling
    // a validator when a cache entry is replaced. No load crosses the span.
    template <std::size_t Size>
    static inline bool equal_short_code(const std::uint8_t *a, const std::uint8_t *b) {
        if constexpr (Size >= 16) {
            const bool same = !wasm_v128_any_true(wasm_v128_xor(wasm_v128_load(a), wasm_v128_load(b)));
            return same && equal_short_code<Size - 16>(a + 16, b + 16);
        } else if constexpr (Size >= 8) {
            std::uint64_t x, y; std::memcpy(&x, a, 8); std::memcpy(&y, b, 8);
            return x == y && equal_short_code<Size - 8>(a + 8, b + 8);
        } else if constexpr (Size >= 4) {
            std::uint32_t x, y; std::memcpy(&x, a, 4); std::memcpy(&y, b, 4);
            return x == y && equal_short_code<Size - 4>(a + 4, b + 4);
        } else {
            static_assert(Size == 0);
            return true;
        }
    }
#endif

#ifdef __wasm_simd128__
    template <std::size_t Size>
    static bool compare_fixed_snapshot(const std::uint8_t *a, const std::uint8_t *b, std::size_t) {
        return equal_short_code<Size>(a, b);
    }
#endif

    code_comparator select_code_comparator(std::size_t size) {
#ifdef __wasm_simd128__
        switch (size) {
        case 0: return compare_fixed_snapshot<0>;
        case 4: return compare_fixed_snapshot<4>;
        case 8: return compare_fixed_snapshot<8>;
        case 12: return compare_fixed_snapshot<12>;
        case 16: return compare_fixed_snapshot<16>;
        case 20: return compare_fixed_snapshot<20>;
        case 24: return compare_fixed_snapshot<24>;
        case 28: return compare_fixed_snapshot<28>;
        case 32: return compare_fixed_snapshot<32>;
        case 36: return compare_fixed_snapshot<36>;
        case 40: return compare_fixed_snapshot<40>;
        case 44: return compare_fixed_snapshot<44>;
        case 48: return compare_fixed_snapshot<48>;
        case 52: return compare_fixed_snapshot<52>;
        case 56: return compare_fixed_snapshot<56>;
        case 60: return compare_fixed_snapshot<60>;
        case 64: return compare_fixed_snapshot<64>;
        default: break;
        }
#endif
        return equal_code_bytes;
    }

    bool equal_code_bytes(const std::uint8_t *a, const std::uint8_t *b, std::size_t size) {
#ifdef __wasm_simd128__
        if (code_compare_mode == 3) {
            // Dense, bounded specialization for word-sized code spans. All
            // other lengths retain the grouped scanner and exact tail path.
            switch (size) {
            case 0: return equal_short_code<0>(a, b);
            case 4: return equal_short_code<4>(a, b);
            case 8: return equal_short_code<8>(a, b);
            case 12: return equal_short_code<12>(a, b);
            case 16: return equal_short_code<16>(a, b);
            case 20: return equal_short_code<20>(a, b);
            case 24: return equal_short_code<24>(a, b);
            case 28: return equal_short_code<28>(a, b);
            case 32: return equal_short_code<32>(a, b);
            case 36: return equal_short_code<36>(a, b);
            case 40: return equal_short_code<40>(a, b);
            case 44: return equal_short_code<44>(a, b);
            case 48: return equal_short_code<48>(a, b);
            case 52: return equal_short_code<52>(a, b);
            case 56: return equal_short_code<56>(a, b);
            case 60: return equal_short_code<60>(a, b);
            case 64: return equal_short_code<64>(a, b);
            default: break;
            }
        }
        if (code_compare_mode == 2 || code_compare_mode == 3 || code_compare_mode == 4) {
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
