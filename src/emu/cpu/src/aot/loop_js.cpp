#if defined(EKA2L1_COUNTED_DIRECT_JS)
#include <cpu/aot/direct_js.h>
#endif
#include <cpu/aot/loop_js.h>
#include <cpu/aot/semantic_js.h>
#include <cpu/aot/counted_js.h>
#include <cpu/dyncom/armstate.h>
#include <cpu/arm_interface.h>
#include <algorithm>
#if defined(EKA2L1_PACKED_PIXEL_JS)
#include <cpu/aot/packed_pixel_pattern.h>
#include <array>
#include <cstring>
#include <limits>
#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif
#endif
#include <stdexcept>

namespace eka2l1::arm::loop_js {
    bool enabled = false;
    static executor invoke = nullptr;
    static bool tracking = false;
    static std::uint64_t call_count = 0, instruction_count = 0;
#if defined(EKA2L1_PACKED_PIXEL_JS)
    bool packed_pixel_enabled = false;
    static constexpr unsigned pixel_chunk_limit = 64, pixel_table_limit = 4;
    static std::array<std::uint32_t, pixel_table_limit> pixel_profiles{};
    static unsigned pixel_profile_count = 0, pixel_prepared_count = 0;
    static std::uint64_t pixel_call_count = 0, pixel_iteration_count = 0, pixel_decline_count = 0;
    std::uint8_t prepare_packed_pixel(const packed_pixel_pattern &pattern) {
#if defined(EKA2L1_COUNTED_DIRECT_JS)
        direct_js::require_idle();
#endif
        const auto key = packed_pixel_profile(pattern);
        for (unsigned n = 0; n < pixel_profile_count; ++n)
            if (pixel_profiles[n] == key) return n;
        if (pixel_profile_count == pixel_table_limit) return 255;
        pixel_profiles[pixel_profile_count] = key;
        return pixel_profile_count++;
    }
#ifdef __EMSCRIPTEN__
    EM_JS(int, prepare_pixel_tables, (const std::uint32_t *profiles, unsigned count), {
        try {
            const tables = [];
            for (let n = 0; n < count; ++n) {
                const key = HEAPU32[(profiles >>> 2) + n];
                const a = key & 31, b = (key >>> 5) & 31, c = (key >>> 10) & 31;
                const x = (key >>> 15) & 31, y = (key >>> 20) & 31, z = (key >>> 25) & 31;
                const table = new Uint32Array(65536);
                for (let v = 0; v < 65536; ++v)
                    table[v] = ((((v >>> a) & 31) << x) + (((v >>> b) & 31) << y)
                        + (((v >>> c) & 31) << z)) >>> 0;
                tables.push(table);
            }
            Module['__packedPixelTables'] = tables;
            Module['__packedPixelError'] = null;
            return 1;
        } catch (error) { Module['__packedPixelError'] = String(error); return -1; }
    });
    EM_JS(void, clear_pixel_tables, (), { delete Module['__packedPixelTables']; });
    EM_JS(unsigned, pixel_words, (unsigned source, unsigned destination, unsigned iterations, unsigned table_index), {
        const table = Module['__packedPixelTables'][table_index];
        const heap = HEAPU32;
        let src = source >>> 2, dst = destination >>> 2, value = 0;
        for (let n = 0; n < iterations; ++n) {
            value = heap[src++];
            const low = table[value & 65535], high = table[value >>> 16];
            heap[dst] = low;
            heap[dst + 1] = high;
            dst += 2;
        }
        return value >>> 0;
    });
#else
    static int prepare_pixel_tables(const std::uint32_t *, unsigned) { return 1; }
    static void clear_pixel_tables() {}
    static std::uint32_t pixel_transform(std::uint32_t value, std::uint32_t key) {
        std::uint32_t output = 0;
        for (unsigned n = 0; n < 3; ++n)
            output += ((value >> ((key >> (n * 5)) & 31)) & 31) << ((key >> ((n + 3) * 5)) & 31);
        return output;
    }
    static unsigned pixel_words(std::uintptr_t source, std::uintptr_t destination,
            unsigned iterations, unsigned table_index) {
        std::uint32_t value = 0;
        for (unsigned n = 0; n < iterations; ++n) {
            std::memcpy(&value, reinterpret_cast<const void *>(source + n * 4), 4);
            const std::uint32_t low = pixel_transform(value & 65535, pixel_profiles[table_index]);
            const std::uint32_t high = pixel_transform(value >> 16, pixel_profiles[table_index]);
            std::memcpy(reinterpret_cast<void *>(destination + n * 8), &low, 4);
            std::memcpy(reinterpret_cast<void *>(destination + n * 8 + 4), &high, 4);
        }
        return value;
    }
#endif
    int configure_packed_pixel(bool on) {
#if defined(EKA2L1_COUNTED_DIRECT_JS)
        direct_js::require_idle();
#endif
        packed_pixel_enabled = false;
        pixel_prepared_count = 0;
        pixel_call_count = pixel_iteration_count = pixel_decline_count = 0;
        if (on && (!enabled || !counted_js::enabled)) { clear_pixel_tables(); return -1; }
        if (on && prepare_pixel_tables(pixel_profiles.data(), pixel_profile_count) != 1) {
            clear_pixel_tables(); return -1;
        }
        if (!on) clear_pixel_tables();
        pixel_prepared_count = on ? pixel_profile_count : 0;
        packed_pixel_enabled = on;
        return 1;
    }
    static std::uint32_t decline_pixel() {
        if (tracking) ++pixel_decline_count;
        return 0;
    }
    static bool pixel_overlaps(std::uintptr_t a, std::size_t an, std::uintptr_t b, std::size_t bn) {
        return a < b + bn && b < a + an;
    }
    static std::uint32_t execute_packed_pixel(ARMul_State *cpu, unsigned remaining,
            const counted_js::loop_admission &proof) {
        const auto &p = *proof.packed_pixel;
        if (p.table >= pixel_prepared_count || cpu->Reg[p.mask] != 31 || cpu->InBigEndianMode()
            || cpu->TFlag || cpu->aot_exit || (!cpu->NirqSig && !(cpu->Cpsr & 0x80))) return decline_pixel();
        const auto source = cpu->Reg[p.source], destination = cpu->Reg[p.destination];
        if ((source | destination) & 3u) return decline_pixel();
        auto iterations = std::min(remaining / 18u, pixel_chunk_limit);
        const auto before = cpu->Reg[p.counter];
        // BNE with initial zero wraps and continues; otherwise stop exactly at zero.
        if (before) iterations = std::min(iterations, before);
        iterations = std::min(iterations, (4096u - (source & 4095u)) / 4u);
        iterations = std::min(iterations, (4096u - (destination & 4095u)) / 8u);
        if (!iterations || source > 0xffffffffu - iterations * 4u
            || destination > 0xffffffffu - iterations * 8u) return decline_pixel();
        auto *tlb = cpu->mem_cache_;
        if (!tlb || tlb->page_bits != 12) return decline_pixel();
        auto *read = tlb->lookup_access<prot_read>(source);
        auto *write = tlb->lookup_access<prot_write>(destination);
        if (!read || !write) return decline_pixel();
        const auto src = reinterpret_cast<std::uintptr_t>(read), dst = reinterpret_cast<std::uintptr_t>(write);
        const unsigned source_bytes = iterations * 4, destination_bytes = iterations * 8;
        if ((src | dst) & 3u || src > std::numeric_limits<std::uintptr_t>::max() - source_bytes
            || dst > std::numeric_limits<std::uintptr_t>::max() - destination_bytes
            || pixel_overlaps(src, source_bytes, reinterpret_cast<std::uintptr_t>(cpu), sizeof(*cpu))
            || pixel_overlaps(dst, destination_bytes, reinterpret_cast<std::uintptr_t>(cpu), sizeof(*cpu))
            || pixel_overlaps(src, source_bytes, proof.host, proof.words * 4u)
            || pixel_overlaps(dst, destination_bytes, proof.host, proof.words * 4u)
            || tlb->entries[tlb->index(source)].execute_addr == (source & ~4095u)
            || tlb->entries[tlb->index(destination)].execute_addr == (destination & ~4095u)) return decline_pixel();
        // Tags prove ordinary callback-free RAM. Preserve each original load,
        // first store and second store before reading the next source word.
        const std::uint32_t value = pixel_words(src, dst, iterations, p.table);
        cpu->Reg[p.value] = value;
        std::uint32_t low = 0, high = 0;
        for (unsigned n = 0; n < 3; ++n) {
            const auto first = (value >> p.input_shift[n]) & 31u;
            const auto second = (value >> (p.input_shift[n] + 16)) & 31u;
            low += first << p.output_shift[n]; high += second << p.output_shift[n];
            cpu->Reg[p.channels[n]] = second;
        }
        cpu->Reg[p.outputs[0]] = low; cpu->Reg[p.outputs[1]] = high;
        cpu->Reg[p.source] = source + source_bytes;
        cpu->Reg[p.destination] = destination + destination_bytes;
        const auto last_before = before - iterations + 1u, after = before - iterations;
        cpu->Reg[p.counter] = after;
        cpu->NFlag = after >> 31; cpu->ZFlag = after == 0;
        cpu->CFlag = last_before != 0; cpu->VFlag = last_before == 0x80000000u;
        // The final non-S ADD immediate8 copies the SUBS carry into the scratch.
        cpu->shifter_carry_out = cpu->CFlag;
        cpu->Reg[15] = proof.pc + (after == 0 ? 18u * 4 : 0u);
        if (tracking) { ++pixel_call_count; pixel_iteration_count += iterations; }
        return iterations * 18u | complete_bit;
    }
    std::uint64_t packed_pixel_calls() { return pixel_call_count; }
    std::uint64_t packed_pixel_iterations() { return pixel_iteration_count; }
    std::uint64_t packed_pixel_declines() { return pixel_decline_count; }
#endif

    void reset() {
#if defined(EKA2L1_COUNTED_DIRECT_JS)
        direct_js::require_idle();
#endif
#if defined(EKA2L1_PACKED_PIXEL_JS)
        configure_packed_pixel(false); pixel_profile_count = 0;
#endif
        semantic_js::reset();
        enabled = tracking = false; invoke = nullptr;
        call_count = instruction_count = 0;
        counted_js::clear_loops();
    }
    void install(executor function, bool diagnostics) {
#if defined(EKA2L1_COUNTED_DIRECT_JS)
        direct_js::require_idle();
#endif
        if (enabled || !function || !counted_js::enabled) throw std::runtime_error("Invalid loop installation");
        invoke = function; tracking = diagnostics; enabled = true;
    }
    static void publish(ARMul_State *cpu) {
        cpu->Cpsr = (cpu->Cpsr & 0x0fffffdfu) | (cpu->NFlag << 31) | (cpu->ZFlag << 30)
            | (cpu->CFlag << 29) | (cpu->VFlag << 28) | (cpu->TFlag << 5);
    }
    std::uint32_t execute_admitted(ARMul_State *cpu, std::uint32_t prefix,
            const counted_js::loop_admission &proof) {
        if (!enabled || !invoke || cpu->TFlag || (!cpu->NirqSig && !(cpu->Cpsr & 0x80))) return 0;
        auto *mapping = cpu->parent()->code_mapping_generation;
        if (proof.word_memory && !mapping) return 0;
        const auto remaining = static_cast<std::uint32_t>(std::min<std::uint64_t>(
            cpu->NumInstrsToExecute - prefix, progress_limit));
        if (proof.semantic) {
            publish(cpu);
            const auto count = semantic_js::execute(cpu, remaining, proof.function & ~semantic_js::function_bit);
            if (!count) return 0;
            publish(cpu);
            return count | complete_bit;
        }
        const auto key = cpu->make_instruction_cache_key(cpu->Reg[15]);
        const auto &slot = cpu->block_l1_cache[ARMul_State::block_l1_index(key)];
        const auto address = [](const auto *value) {
            return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(value));
        };
        const invocation_context context{remaining, address(&cpu->trans_gen), address(mapping),
            address(&cpu->instruction_cache_asid), address(&slot.key), address(&slot.ptr), address(&cpu->mem_cache_)};
        auto *tlb = cpu->mem_cache_;
        cpu->aot_tlb = tlb && tlb->page_bits == 12 ? address(tlb->entries) : 0;
        cpu->aot_code_begin = proof.host; cpu->aot_code_end = proof.host + proof.words * 4u;
        publish(cpu); cpu->aot_exit = 0;
#if defined(EKA2L1_PACKED_PIXEL_JS)
        auto result = packed_pixel_enabled && proof.packed_pixel
            ? execute_packed_pixel(cpu, remaining, proof) : 0u;
        if (!result) result = invoke(cpu, proof.function, &context);
#else
        const auto result = invoke(cpu, proof.function, &context);
#endif
        // A function may have performed effects. Invalid progress never retries.
        if (result & ~(progress_limit | complete_bit))
            throw std::runtime_error("Invalid counted loop progress bits");
        const auto count = proof.word_memory ? result & progress_limit : result;
        const bool complete = !proof.word_memory || (result & complete_bit);
        if (!count || count > remaining || cpu->TFlag
            || (complete ? count % proof.words != 0
                || (cpu->Reg[15] != proof.pc && cpu->Reg[15] != proof.pc + proof.words * 4u)
                : count % proof.words == 0 || cpu->Reg[15] != proof.pc + (count % proof.words) * 4u))
            throw std::runtime_error("Invalid counted loop progress after admission");
        if (!complete && cpu->NumInstrsToExecute) counted_js::resume_loop(cpu, proof, count % proof.words);
        publish(cpu);
        if (tracking) { ++call_count; instruction_count += count; }
        return count | (complete ? complete_bit : 0u);
    }
    std::uint32_t execute_progress(ARMul_State *cpu, std::uint32_t prefix) {
        if (!enabled || cpu->TFlag || (!cpu->NirqSig && !(cpu->Cpsr & 0x80))) return 0;
        counted_js::loop_admission proof;
        if (!counted_js::admit_loop(cpu, prefix, proof)) return 0;
        return execute_admitted(cpu, prefix, proof);
    }
    std::uint32_t execute(ARMul_State *cpu, std::uint32_t prefix) {
        const auto result = execute_progress(cpu, prefix);
        if (result && !(result & complete_bit)) throw std::runtime_error("Partial loop requires progress ABI");
        return result & progress_limit;
    }
    std::uint64_t calls() { return call_count; }
    std::uint64_t instructions() { return instruction_count; }
}
