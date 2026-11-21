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
#include <cpu/aot/palette_pixel_pattern.h>
#include <array>
#include <atomic>
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
    static std::uint64_t palette_call_count = 0, palette_iteration_count = 0, palette_instruction_count = 0;
    static std::uint64_t palette_aligned_count = 0, palette_unaligned_count = 0, palette_decline_count = 0;
    struct palette_result {
        std::uint32_t value;
        std::array<std::uint16_t, 8> colors;
    };
    static_assert(sizeof(palette_result) == 20 && offsetof(palette_result, colors) == 4);
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
    EM_JS(void, palette_words, (unsigned source, unsigned palette, unsigned destination,
            unsigned stride, unsigned iterations, unsigned shifted, palette_result *result), {
        const words = HEAPU32, halves = HEAPU16, table = palette >>> 1;
        let src = source >>> 2, dst = destination, value = 0;
        let a = 0, b = 0, c = 0, d = 0, e = 0, f = 0, g = 0, h = 0;
        for (let n = 0; n < iterations; ++n) {
            value = words[src++];
            a = halves[table + (value & 15)];
            b = halves[table + ((value >>> 4) & 15)];
            c = halves[table + ((value >>> 8) & 15)];
            d = halves[table + ((value >>> 12) & 15)];
            e = halves[table + ((value >>> 16) & 15)];
            f = halves[table + ((value >>> 20) & 15)];
            g = halves[table + ((value >>> 24) & 15)];
            h = halves[table + (value >>> 28)];
            if (shifted) {
                halves[dst >>> 1] = a;
                const aligned = (dst + 2) >>> 2;
                words[aligned] = b | (c << 16);
                words[aligned + 1] = d | (e << 16);
                words[aligned + 2] = f | (g << 16);
                halves[(dst + 14) >>> 1] = h;
            } else {
                const aligned = dst >>> 2;
                words[aligned] = a | (b << 16);
                words[aligned + 1] = c | (d << 16);
                words[aligned + 2] = e | (f << 16);
                words[aligned + 3] = g | (h << 16);
            }
            dst += stride;
        }
        // Return native scratch values without rereading any guest memory.
        words[result >>> 2] = value;
        const colors = (result + 4) >>> 1;
        halves[colors] = a; halves[colors + 1] = b;
        halves[colors + 2] = c; halves[colors + 3] = d;
        halves[colors + 4] = e; halves[colors + 5] = f;
        halves[colors + 6] = g; halves[colors + 7] = h;
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
    static void palette_words(std::uintptr_t source, std::uintptr_t palette, std::uintptr_t destination,
            unsigned stride, unsigned iterations, unsigned shifted, palette_result *result) {
        for (unsigned n = 0; n < iterations; ++n) {
            std::memcpy(&result->value, reinterpret_cast<const void *>(source + n * 4), 4);
            for (unsigned pixel = 0; pixel < 8; ++pixel)
                std::memcpy(&result->colors[pixel], reinterpret_cast<const void *>(
                    palette + ((result->value >> (pixel * 4)) & 15u) * 2), 2);
            const auto &c = result->colors;
            if (shifted) {
                std::memcpy(reinterpret_cast<void *>(destination), &c[0], 2);
                for (unsigned pixel = 0; pixel < 3; ++pixel) {
                    const std::uint32_t word = c[pixel * 2 + 1] | (std::uint32_t(c[pixel * 2 + 2]) << 16);
                    std::memcpy(reinterpret_cast<void *>(destination + 2 + pixel * 4), &word, 4);
                }
                std::memcpy(reinterpret_cast<void *>(destination + 14), &c[7], 2);
            } else {
                for (unsigned pixel = 0; pixel < 4; ++pixel) {
                    const std::uint32_t word = c[pixel * 2] | (std::uint32_t(c[pixel * 2 + 1]) << 16);
                    std::memcpy(reinterpret_cast<void *>(destination + pixel * 4), &word, 4);
                }
            }
            destination += stride;
        }
    }
#endif
    int configure_packed_pixel(bool on) {
#if defined(EKA2L1_COUNTED_DIRECT_JS)
        direct_js::require_idle();
#endif
        packed_pixel_enabled = false;
        pixel_prepared_count = 0;
        pixel_call_count = pixel_iteration_count = pixel_decline_count = 0;
        palette_call_count = palette_iteration_count = palette_instruction_count = 0;
        palette_aligned_count = palette_unaligned_count = palette_decline_count = 0;
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
    static std::uint32_t decline_palette() {
        if (tracking) ++palette_decline_count;
        return 0;
    }
    static std::uint32_t execute_palette_pixel(ARMul_State *cpu, unsigned prefix, unsigned remaining,
            const counted_js::loop_admission &proof, bool &handoff) {
        const auto &p = *proof.palette_pixel;
        auto *core = cpu->parent();
        auto *mapping = core->code_mapping_generation;
        if (!mapping || !core->resolve_data || cpu->Reg[p.mask] != 30 || cpu->InBigEndianMode()
            || cpu->TFlag || cpu->aot_exit || (!cpu->NirqSig && !(cpu->Cpsr & 0x80))) return decline_palette();
        const auto source = cpu->Reg[p.source], destination = cpu->Reg[p.destination];
        const auto palette = cpu->Reg[p.palette], before = cpu->Reg[p.counter];
        if ((source | destination) & 3u || (palette & 1u)
            || destination > 0xffffffffu - p.stride || palette > 0xffffffffu - 32u) return decline_palette();
        const auto first = destination + p.stride - (p.shifted ? 2u : 0u);
        if ((first & 4095u) > 4096u - 16u || (palette & 4095u) > 4096u - 32u) return decline_palette();
        auto iterations = std::min(remaining / proof.words, pixel_chunk_limit);
        if (before) iterations = std::min(iterations, before);
        iterations = std::min(iterations, (4096u - (source & 4095u)) / 4u);
        iterations = std::min(iterations, 1u + (4096u - (first & 4095u) - 16u) / p.stride);
        if (!iterations || source > 0xffffffffu - iterations * 4u
            || destination > 0xffffffffu - iterations * p.stride) return decline_palette();
        const unsigned source_bytes = iterations * 4u, destination_bytes = (iterations - 1u) * p.stride + 16u;
        const auto generation = cpu->trans_gen;
        const auto asid = cpu->instruction_cache_asid;
        const auto epoch = mapping->load(std::memory_order_acquire);
        const auto budget = cpu->NumInstrsToExecute;
        const auto unchanged = [&]() {
            return cpu->trans_gen == generation && cpu->instruction_cache_asid == asid
                && core->code_mapping_generation == mapping
                && mapping->load(std::memory_order_acquire) == epoch && cpu->NumInstrsToExecute == budget
                && cpu->Reg[15] == proof.pc && cpu->Reg[p.source] == source && cpu->Reg[p.destination] == destination
                && cpu->Reg[p.palette] == palette && cpu->Reg[p.counter] == before && cpu->Reg[p.mask] == 30
                && !cpu->TFlag && !cpu->InBigEndianMode() && !cpu->aot_exit
                && (cpu->NirqSig || (cpu->Cpsr & 0x80));
        };
        const auto refuse = [&]() {
            handoff = !unchanged();
            return decline_palette();
        };
        const auto span = [&](std::uint32_t address, unsigned bytes, bool write) -> std::uintptr_t {
            eka2l1::arm::core::data_mapping view{};
            if (!core->resolve_data(address, write, view) || view.address_space != asid
                || !view.bytes || view.size < bytes) return 0;
            const auto host = reinterpret_cast<std::uintptr_t>(view.bytes);
            return host <= std::numeric_limits<std::uintptr_t>::max() - bytes ? host : 0;
        };
        const auto src = span(source, source_bytes, false);
        if (!src || !unchanged()) return refuse();
        const auto table = span(palette, 32, false);
        if (!table || !unchanged()) return refuse();
        const auto dst = span(first, destination_bytes, true);
        if (!dst || !unchanged()) return refuse();
        palette_result result{};
        const auto unsafe = [&](std::uintptr_t host, unsigned bytes) {
            return pixel_overlaps(host, bytes, reinterpret_cast<std::uintptr_t>(cpu), sizeof(*cpu))
                || pixel_overlaps(host, bytes, reinterpret_cast<std::uintptr_t>(core), sizeof(*core))
                || pixel_overlaps(host, bytes, proof.host, proof.words * 4u)
                || pixel_overlaps(host, bytes, reinterpret_cast<std::uintptr_t>(&proof), sizeof(proof))
                || pixel_overlaps(host, bytes, reinterpret_cast<std::uintptr_t>(&p), sizeof(p))
                || pixel_overlaps(host, bytes, reinterpret_cast<std::uintptr_t>(&result), sizeof(result));
        };
        if ((src & 3u) || (table & 1u) || ((dst + (p.shifted ? 2u : 0u)) & 3u)
            || unsafe(src,source_bytes) || unsafe(table,32) || unsafe(dst,destination_bytes)
            || pixel_overlaps(src,source_bytes,table,32) || pixel_overlaps(src,source_bytes,dst,destination_bytes)
            || pixel_overlaps(table,32,dst,destination_bytes)
            || budget < prefix || budget - prefix < iterations * proof.words) return decline_palette();
        // The resolver admits owned, non-executable normal RAM. A single chunk
        // retains original source/palette load and sparse destination-store order.
        palette_words(src, table, dst, p.stride, iterations, p.shifted, &result);
        const auto &c = result.colors;
        const auto packed = [&](unsigned low, unsigned high) { return c[low] | (std::uint32_t(c[high]) << 16); };
        cpu->Reg[p.value] = result.value;
        if (p.shifted) {
            cpu->Reg[p.work[0]] = c[0]; cpu->Reg[p.work[1]] = packed(1,2);
            cpu->Reg[p.work[2]] = packed(3,4); cpu->Reg[p.work[3]] = packed(5,6);
            cpu->Reg[p.work[4]] = c[7]; cpu->Reg[p.work[5]] = c[5];
        } else {
            for (unsigned n = 0; n < 4; ++n) cpu->Reg[p.work[n]] = packed(n*2,n*2+1);
            cpu->Reg[p.work[4]] = c[6]; cpu->Reg[p.work[5]] = c[7];
        }
        cpu->Reg[p.source] = source + source_bytes;
        cpu->Reg[p.destination] = destination + iterations * p.stride;
        const auto last_before = before - iterations + 1u, after = before - iterations;
        cpu->Reg[p.counter] = after;
        cpu->NFlag = after >> 31; cpu->ZFlag = after == 0;
        cpu->CFlag = last_before != 0; cpu->VFlag = last_before == 0x80000000u;
        // The last ORR shifts a zero-extended halfword left16 without setting CPSR.
        cpu->shifter_carry_out = 0;
        cpu->Reg[15] = proof.pc + (after == 0 ? proof.words * 4u : 0u);
        if (tracking) {
            ++palette_call_count; palette_iteration_count += iterations;
            palette_instruction_count += iterations * proof.words;
            (p.shifted ? palette_unaligned_count : palette_aligned_count) += iterations;
        }
        return iterations * proof.words | complete_bit;
    }
    std::uint64_t palette_pixel_calls() { return palette_call_count; }
    std::uint64_t palette_pixel_iterations() { return palette_iteration_count; }
    std::uint64_t palette_pixel_instructions() { return palette_instruction_count; }
    std::uint64_t palette_pixel_aligned_iterations() { return palette_aligned_count; }
    std::uint64_t palette_pixel_unaligned_iterations() { return palette_unaligned_count; }
    std::uint64_t palette_pixel_declines() { return palette_decline_count; }
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
        std::uint32_t result = 0;
        if (packed_pixel_enabled && proof.palette_pixel) {
            bool handoff = false;
            result = execute_palette_pixel(cpu, prefix, remaining, proof, handoff);
            if (handoff) return 0;
        } else if (packed_pixel_enabled && proof.packed_pixel) result = execute_packed_pixel(cpu, remaining, proof);
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
