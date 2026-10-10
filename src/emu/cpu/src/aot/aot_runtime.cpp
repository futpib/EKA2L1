/*
 * Copyright (c) 2024 EKA2L1 Team.
 *
 * This file is part of EKA2L1 project.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <cpu/aot/aot_runtime.h>
#include <cpu/aot/watchdog.h>
#include <cpu/aot/memory_experiment.h>
#include <cpu/aot/aot_registry.h>
#include <cpu/aot/code_cache.h>
#include <cpu/aot/exit_census.h>
#include <cpu/aot/execution_limits.h>
#include <common/performance.h>
#include <common/deterministic.h>
#include <common/guest_profile.h>
#include <cpu/dyncom/armstate.h>
#include <common/log.h>
#include <cpu/dyncom/arm_dyncom.h>
#include <cpu/12l1r/exclusive_monitor.h>
#include <cpu/12l1r/tlb.h>
#include <cpu/aot/arm_translator.h>
#include <algorithm>
#include <unordered_map>
#include <cstdlib>
#include <cstddef>
#include <chrono>
#include <mutex>
#include <unordered_set>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif

namespace eka2l1::arm::aot {
#ifdef __EMSCRIPTEN__
static_assert(offsetof(ARMul_State, NumInstrsToExecute) == state_offsets::NUM_INSTRS_TO_EXECUTE);
#endif
compilation_counters compilation;
struct compilation_timer {
    std::atomic<std::uint64_t> &total;
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    ~compilation_timer() { total.fetch_add(std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - start).count(), std::memory_order_relaxed); }
};
static std::atomic<std::uint64_t> completed_function_count{0};
std::uint64_t compiled_function_count() { return completed_function_count.load(std::memory_order_relaxed); }

static std::uint32_t hot_rom_base = 0, hot_rom_size = 0;
common::diagnostics::flag diagnostics_enabled = false;

// Extend export-based compilation using deterministic dispatch samples. Only
// immutable ROM addresses are eligible; RAM code needs explicit invalidation.
bool hot_compilation_enabled = false;
bool ram_compilation_enabled = false;
bool chaining_enabled = false;
static bool region_enabled = false;
static arm_ir_policy ir_policy = arm_ir_policy::configured;
#ifdef EKA2L1_WASM_DEFER_MEMORY
static constexpr bool defer_memory_enabled = true;
#else
static constexpr bool defer_memory_enabled = false;
#endif
static validated_code_cache ram_cache;
static const validated_code_cache::block *census_entry = nullptr;
static std::unordered_map<std::uint64_t, unsigned> ram_counts;
static const std::uint8_t *hot_rom = nullptr;
static std::uint64_t hot_dispatches = 0;
static std::uint32_t hot_compiled = 0;
static std::unordered_map<std::uint32_t, unsigned> hot_counts;
static std::vector<wasm_func_def> hot_pending;
struct precompile_image {
    std::uint32_t space, base, size;
    std::vector<std::uint32_t> entries;
};
static std::mutex precompile_mutex;
static std::vector<precompile_image> precompile_images;
static std::atomic<bool> precompile_pending{false};

void queue_precompile_image(std::uint32_t space, std::uint32_t base,
        const std::uint8_t *bytes, std::uint32_t size, std::vector<std::uint32_t> entries) {
#ifdef __EMSCRIPTEN__
    if (!hot_compilation_enabled || !bytes || !size || std::uint64_t(base) + size > 0x100000000ull) return;
    // Function pointers in literal pools/vtables find non-exported callbacks.
    // These are candidates only: never execute them, and validate executable
    // mappings again on the worker. No guest pointer survives this callback.
    const bool rom_image = base - hot_rom_base < hot_rom_size;
    for (std::uint32_t i = 0; !rom_image && i + 4 <= size; i += 4) {
        std::uint32_t target; std::memcpy(&target, bytes + i, 4);
        const auto pc = target & ~1u;
        if ((pc - base < size || pc - hot_rom_base < hot_rom_size)
            && ((target & 1) || !(pc & 3))) entries.push_back(target);
    }
    const std::lock_guard<std::mutex> lock(precompile_mutex);
    precompile_images.push_back({space, base, size, std::move(entries)});
    precompile_pending.store(true, std::memory_order_release);
#endif
}

static void flush_hot_blocks() {
    if (hot_pending.empty()) return;
    std::vector<wasm_import_func> imports{{"env","tlb_read32",2,true},{"env","tlb_write32",3,false},
        {"env","tlb_read8",2,true},{"env","tlb_write8",3,false},
        {"env","tlb_read16",2,true},{"env","tlb_write16",3,false}};
    if (arm_exclusive_memory) imports.push_back({"env","arm_exclusive",2,false});
    auto bytes = [&] {
        compilation_timer timer{compilation.emission_us};
        return build_wasm_module(hot_pending, imports);
    }();
    stage_aot_module(std::move(bytes), "hot-rom");
    instantiate_staged_modules();
    hot_pending.clear();
}

void configure_hot_rom(const std::uint8_t *host, std::uint32_t base, std::uint32_t size, bool enabled) {
    hot_rom = host; hot_rom_base = base; hot_rom_size = size;
    global_registry().configure_rom_index(base, size, sparse_rom_lookup_enabled);
    hot_compilation_enabled = enabled;
    completed_function_count = 0;
    compilation.translation_us = 0; compilation.emission_us = 0; compilation.installation_us = 0;
    compilation.eager_functions = 0; compilation.eager_passes = 0;
    { const std::lock_guard<std::mutex> lock(precompile_mutex);
      precompile_images.clear(); precompile_pending = false; }
    hot_dispatches = 0; hot_compiled = 0; hot_counts.clear(); hot_pending.clear();
    ram_cache = {}; ram_counts.clear();
    const char *ram = std::getenv("EKA2L1_AOT_RAM");
    ram_compilation_enabled = enabled && ram && ram[0] == '1';
    const char *chain = std::getenv("EKA2L1_AOT_CHAIN");
    chaining_enabled = enabled && chain && chain[0] == '1';
    const char *region = std::getenv("EKA2L1_AOT_REGION");
    region_enabled = chaining_enabled && region && region[0] == '1';
    ir_policy = arm_ir_policy::configured;
    const char *ir = std::getenv("EKA2L1_AOT_IR_MODE");
    parse_arm_ir_policy(ir, ir_policy);
    if (enabled) fprintf(stderr, "AOT: ir_policy=%d\n", static_cast<int>(ir_policy));
}

void invalidate_ram_code(std::uint32_t address, std::size_t size) {
    ram_cache.invalidate(address, size);
}

template<bool Profile, bool TrustBytes = false>
#if defined(_MSC_VER)
__forceinline
#else
__attribute__((always_inline))
#endif
static aot_func lookup_compiled_impl(ARMul_State *cpu, const registry &functions) {
    if constexpr(Profile) if(exit_census::enabled)census_entry=nullptr;
    // TrustBytes follows the frozen code-cache policy.
    const auto pc = cpu->Reg[15], pc_mode = pc | cpu->TFlag;
    // Existing ROM functions use immutable bytes and need no mapping lookup.
    if (!ram_compilation_enabled || (pc >= hot_rom_base && pc - hot_rom_base < hot_rom_size)) {
        // Trusted-byte chains emit no interval readers. Keep publication for
        // mutation-compatible execution and diagnostic/reference paths.
        if constexpr(!TrustBytes) cpu->aot_code_begin = cpu->aot_code_end = 0;
        auto function = functions.lookup(pc_mode);
        if (Profile && (common::guest_profile::enabled && common::performance::counting()) && !function) common::guest_profile::state.event("rom_missing",pc_mode);
        return function;
    }
    if constexpr(TrustBytes && !Profile)
        return ram_cache.lookup_trusted(pc_mode, *cpu->parent());
    // The normal path avoids the resolver callback on a generation/space hit.
    // Mode 0 also compares compiled bytes; trusted-byte modes omit those scans.
    if (!Profile || !(common::guest_profile::enabled && common::performance::counting())) {
        auto *entry = TrustBytes ? ram_cache.find_trusted_original(pc_mode, *cpu->parent())
            : ram_cache.find(pc_mode, *cpu->parent());
        if (!entry) return nullptr;
        if constexpr(Profile) if(exit_census::enabled)census_entry=entry;
        if constexpr(!TrustBytes) {
            cpu->aot_code_begin = static_cast<std::uint32_t>(entry->guard_begin);
            cpu->aot_code_end = static_cast<std::uint32_t>(entry->guard_end);
        }
        return entry->function;
    }
    core::code_mapping view;
    if (!cpu->parent()->resolve_code) return nullptr;
    const bool mapped = cpu->parent()->resolve_code(pc, view);
    auto *entry = TrustBytes ? ram_cache.find_trusted_original(pc_mode, *cpu->parent())
            : ram_cache.find(pc_mode, *cpu->parent());
    if (Profile && (common::guest_profile::enabled && common::performance::counting()) && (!mapped || !entry || !entry->function)) {
        std::uint32_t opcode = 0;
        if (mapped && view.bytes && view.size >= (cpu->TFlag ? 2u : 4u)) std::memcpy(&opcode,view.bytes,cpu->TFlag ? 2 : 4);
        common::guest_profile::state.event(!mapped ? "ram_unmapped" : !entry ? "ram_missing" : entry->rejected ? "ram_rejected" : "ram_pending",pc_mode,view.address_space,opcode);
    }
    if (!mapped || !entry) return nullptr;
    if constexpr(Profile) if(exit_census::enabled)census_entry=entry;
    if constexpr(!TrustBytes) {
        cpu->aot_code_begin = static_cast<std::uint32_t>(entry->guard_begin);
        cpu->aot_code_end = static_cast<std::uint32_t>(entry->guard_end);
    }
    return entry->function;
}

aot_func lookup_compiled(ARMul_State *cpu) {
    if (common::guest_profile::enabled && common::performance::enabled && common::performance::detailed)
        return lookup_compiled_impl<true>(cpu, global_registry());
    if (hotpath_policy == 2 && common::code_tracking::skip_code_scans())
        return lookup_compiled_impl<false, true>(cpu, global_registry());
    return lookup_compiled_impl<false>(cpu, global_registry());
}

template<bool TrustBytes, bool Direct>
static compiled_run execute_chain_impl(ARMul_State *cpu, aot_func function) {
    if constexpr (Direct) {
        if (cpu->parent()->experimental_memory) cpu->aot_tlb = cpu->parent()->experimental_pointer;
    } else {
        auto *tlb = static_cast<dyncom_core *>(cpu->parent())->mem_cache();
        cpu->aot_tlb = tlb->page_bits == 12
            ? static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(tlb->entries)) : 0;
    }
    const auto &functions = global_registry();
    std::uint32_t progress = 0;
    while (function) {
        cpu->aot_exit = 0;
        progress = function(cpu);
        if (!progress || !cpu->NumInstrsToExecute
            || (!cpu->NirqSig && !(cpu->Cpsr & 0x80)) || watchdog::requested()) break;
        // Each body has completed; indirect successors may cycle.
        cpu->Reg[15] &= cpu->TFlag ? ~1u : ~3u;
        function = lookup_compiled_impl<false, TrustBytes>(cpu, functions);
    }
    return {progress};
}

template<bool Direct> static compiled_run execute_chain_selected(ARMul_State *cpu, aot_func function) {
    if (hotpath_policy == 2 && common::code_tracking::skip_code_scans())
        return execute_chain_impl<true, Direct>(cpu, function);
    return execute_chain_impl<false, Direct>(cpu, function);
}

compiled_run execute_chain(ARMul_State *cpu, aot_func function) {
    return memory_experiment::enabled() ? execute_chain_selected<true>(cpu,function) : execute_chain_selected<false>(cpu,function);
}

std::uint32_t execute_single(ARMul_State *cpu, aot_func function) {
    cpu->aot_exit = 0;
    return function(cpu);
}

#ifdef __EMSCRIPTEN__
static void discover_successors(core &cpu, const translate_result &tr, bool thumb,
        std::vector<std::uint32_t> &successors) {
    successors.insert(successors.end(), tr.dispatch_entries.begin(), tr.dispatch_entries.end());
    for (auto address : tr.resume_points) successors.push_back(address | (thumb ? 1u : 0u));
    // Literal loads find import veneers and address-taken callbacks that direct
    // branch decoding alone misses. Values are candidates, never folded into
    // generated code; the worklist checks loaded executable image bounds.
    for (auto address : tr.literal_refs) {
        const auto offset = std::uint64_t(address) - hot_rom_base;
        const std::uint8_t *bytes = nullptr;
        core::code_mapping view;
        if (address >= hot_rom_base && offset + 4 <= hot_rom_size) bytes = hot_rom + offset;
        else if (cpu.resolve_code && cpu.resolve_code(address, view) && view.size >= 4)
            bytes = view.bytes;
        if (bytes) {
            std::uint32_t target; std::memcpy(&target, bytes, 4);
            successors.push_back(target);
        }
    }
}

static void compile_at(core &cpu, std::uint32_t key, std::vector<std::uint32_t> *successors = nullptr) {
    const auto pc = key & ~1u;
    const bool thumb = key & 1;
    if (pc < hot_rom_base || pc - hot_rom_base >= hot_rom_size) {
        if (!ram_compilation_enabled || !cpu.resolve_code) return;
        if (ram_cache.versions() >= 16384) {
            if ((common::guest_profile::enabled && common::performance::counting())) common::guest_profile::state.event("ram_capacity",key);
            return;
        }
        core::code_mapping view;
        if (!cpu.resolve_code(pc, view)) return;
        if (ram_cache.find(key, cpu)) return; // compiled or awaiting instantiation
        const auto identity = validated_code_cache::key(view.address_space, key);
        if (!successors && ram_counts.size() >= 131072 && !ram_counts.count(identity)) return;
        auto &count = ram_counts[identity];
        if (!successors && ++count % 8) {
            if ((common::guest_profile::enabled && common::performance::counting())) common::guest_profile::state.event("candidate_threshold",key,view.address_space);
            return;
        }
        const auto size = std::min(std::size_t(chaining_enabled ? primary_window_bytes : 256), view.size);
        leaf_resolver leaves = [&](std::uint32_t target) {
            core::code_mapping leaf;
            if (!cpu.resolve_code(target, leaf) || leaf.address_space != view.address_space)
                return std::vector<std::uint8_t>{};
            const auto bytes = std::min(std::size_t(arm_indirect_calls ? primary_window_bytes : leaf_instruction_limit*4), leaf.size);
            return std::vector<std::uint8_t>(leaf.bytes, leaf.bytes + bytes);
        };
        const code_window immutable_code{hot_rom, hot_rom_base, hot_rom_size};
        auto translate = [&] {return thumb ? translate_thumb_block(view.bytes, size, pc, nullptr, nullptr, true, true, chaining_enabled, &immutable_code)
                            : translate_arm_block(view.bytes, size, pc, nullptr, nullptr, true, true, chaining_enabled, region_enabled, &leaves, defer_memory_enabled, ir_policy);};
        auto tr = [&] { compilation_timer timer{compilation.translation_us}; return translate(); }();
        if (tr.func.body.empty() || !tr.entry_supported) {
            // Cache rejection against these exact bytes; retry only after mutation.
            ram_cache.insert(key, view, std::min(size, std::size_t(thumb ? 2 : 4))).rejected = true;
            if ((common::guest_profile::enabled && common::performance::counting())) common::guest_profile::state.event("compile_rejected",key,view.address_space);
            return;
        }
        // Only emitted instructions depend on these bytes. The rest of the
        // translation window is unused after a store/terminator; validating it
        // on millions of short-block entries needlessly scans unrelated code.
        if(exit_census::enabled) {
            ++exit_census::compilation["accepted_regions"];
            exit_census::compile_site(key,static_cast<unsigned>(size),tr.end_address-pc>=size?"source_window_filled":"source_window_not_filled");
            exit_census::compile_site(key,static_cast<unsigned>(tr.dependencies.size()),"inline_dependencies");
        }
        const auto consumed = std::clamp(std::size_t(tr.end_address - pc),
            std::size_t(thumb ? 2 : 4), size);
        auto &entry = ram_cache.insert(key, view, consumed);
        for (const auto &dependency : tr.dependencies) {
            core::code_mapping leaf;
            if (!cpu.resolve_code(dependency.address, leaf)
                || leaf.address_space != view.address_space || leaf.size < dependency.bytes.size()
                || (!common::code_tracking::skip_code_scans()
                    && !equal_code_bytes(leaf.bytes, dependency.bytes.data(), dependency.bytes.size()))) {
                entry.live = false;
                return;
            }
            validated_code_cache::add_dependency(entry, dependency.address, leaf.bytes, dependency.bytes);
        }
        tr.func.export_name = "r_" + std::to_string(entry.version) + "_pc_" + std::to_string(pc);
        if (successors) {
            discover_successors(cpu, tr, thumb, *successors);
            ++compilation.eager_functions;
        }
        hot_pending.push_back(std::move(tr.func));
        if (common::performance::counting()) ++common::performance::ram_blocks_compiled;
        if (hot_pending.size() >= 32) flush_hot_blocks();
        return;
    }
    if (global_registry().lookup(key)) return;
    if (!successors && hot_compiled >= 4096) return;
    if (!successors && hot_counts.size() >= 65536 && !hot_counts.count(key)) return;
    auto &count = hot_counts[key];
    if (count >= 8) return; // one attempt per immutable entry
    if (successors) count = 8;
    else if (++count != 8) return;
    const auto offset = pc - hot_rom_base;
    const auto size = std::min(chaining_enabled ? primary_window_bytes : 128u, hot_rom_size - offset);
    const code_window immutable_code{hot_rom, hot_rom_base, hot_rom_size};
    auto translate = [&] {return thumb ? translate_thumb_block(hot_rom + offset, size, pc, nullptr, nullptr, true, false, chaining_enabled, &immutable_code)
                        : translate_arm_block(hot_rom + offset, size, pc, nullptr, nullptr, true, false, chaining_enabled, region_enabled, nullptr, defer_memory_enabled, ir_policy);};
    auto tr = [&] { compilation_timer timer{compilation.translation_us}; return translate(); }();
    if (tr.func.body.empty() || !tr.entry_supported) return;
    tr.func.export_name = "f_" + std::to_string(key);
    if (successors) {
        discover_successors(cpu, tr, thumb, *successors);
        ++compilation.eager_functions;
    }
    hot_pending.push_back(std::move(tr.func));
    if (hot_pending.size() >= 32) flush_hot_blocks();
    if (!successors) ++hot_compiled;
}
#endif

void prepare_compiled_code(core &cpu) {
#ifdef __EMSCRIPTEN__
    if (!hot_compilation_enabled || !precompile_pending.load(std::memory_order_acquire)) return;
    std::vector<precompile_image> images;
    {
        const std::lock_guard<std::mutex> lock(precompile_mutex);
        for (auto &image : precompile_images) {
            if (image.space == cpu.code_address_space || image.base - hot_rom_base < hot_rom_size) {
                images.push_back({image.space, image.base, image.size, std::move(image.entries)});
            }
        }
        precompile_pending = std::any_of(precompile_images.begin(), precompile_images.end(),
            [](const auto &image) { return !image.entries.empty(); });
    }
    std::vector<std::uint32_t> pending;
    for (auto &image : images) pending.insert(pending.end(), image.entries.begin(), image.entries.end());
    if (pending.empty()) return;
    instantiate_staged_modules();
    const auto begin = std::chrono::steady_clock::now();
    const auto before = compilation.eager_functions.load();
    std::unordered_set<std::uint32_t> visited;
    for (std::size_t i = 0; i < pending.size(); ++i) {
        const auto key = pending[i], pc = key & ~1u;
        if ((!((key & 1) || !(pc & 3))) || !visited.insert(key).second) continue;
        if (std::none_of(images.begin(), images.end(), [&](const auto &image) { return pc - image.base < image.size; })) continue;
        compile_at(cpu, key, &pending);
    }
    flush_hot_blocks();
    ++compilation.eager_passes;
    fprintf(stderr, "AOT: precompiled %llu regions before execution in space %u (%.1f ms)\n",
        static_cast<unsigned long long>(compilation.eager_functions.load() - before), cpu.code_address_space,
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count());
#endif
}

void observe_hot_pc(ARMul_State *cpu) {
#ifdef __EMSCRIPTEN__
    if (!hot_compilation_enabled) return;
    ++hot_dispatches;
    if (hot_dispatches & 31) return;
    if ((hot_dispatches & 8191) == 0) flush_hot_blocks();
    compile_at(*cpu->parent(), cpu->Reg[15] | cpu->TFlag);
#endif
}

#ifdef __EMSCRIPTEN__

// Generated arithmetic keeps NZCVT separate from the packed CPSR. Publish them
// after the emitter's state barrier and before a memory/exception callback reads
// the owning core's CPSR. Direct mapped accesses do not call these trampolines.
template<bool Direct> struct memory_callback_scope {
    ARMul_State *state;
    explicit memory_callback_scope(ARMul_State *s):state(s) {}
    ~memory_callback_scope() {
        if constexpr(Direct) {
            state->parent()->publish_memory_view();
            state->aot_tlb=state->parent()->experimental_pointer;
        }
    }
};
static void publish_callback_cpsr(ARMul_State *state) {
    state->Cpsr = (state->Cpsr & 0x0fffffdfu) | (state->NFlag << 31)
        | (state->ZFlag << 30) | (state->CFlag << 29) | (state->VFlag << 28)
        | (state->TFlag << 5);
}

// C functions exported for the AOT WASM module to call back into.
// These are the memory access trampolines.
extern "C" {
    EMSCRIPTEN_KEEPALIVE
    std::uint32_t aot_tlb_read32(ARMul_State *state, std::uint32_t arm_addr) {
        publish_callback_cpsr(state);
        return state->ReadMemory32(arm_addr);
    }

    EMSCRIPTEN_KEEPALIVE
    void aot_tlb_write32(ARMul_State *state, std::uint32_t arm_addr, std::uint32_t value) {
        publish_callback_cpsr(state);
        state->WriteMemory32(arm_addr, value);
    }

    EMSCRIPTEN_KEEPALIVE
    std::uint32_t aot_tlb_read8(ARMul_State *state, std::uint32_t arm_addr) {
        publish_callback_cpsr(state);
        return state->ReadMemory8(arm_addr);
    }

    EMSCRIPTEN_KEEPALIVE
    std::uint32_t aot_tlb_read16(ARMul_State *state, std::uint32_t arm_addr) {
        publish_callback_cpsr(state);
        return state->ReadMemory16(arm_addr);
    }

    EMSCRIPTEN_KEEPALIVE
    void aot_tlb_write16(ARMul_State *state, std::uint32_t arm_addr, std::uint32_t value) {
        publish_callback_cpsr(state);
        state->WriteMemory16(arm_addr, value);
    }

    EMSCRIPTEN_KEEPALIVE
    void aot_tlb_write8(ARMul_State *state, std::uint32_t arm_addr, std::uint32_t value) {
        publish_callback_cpsr(state);
        state->WriteMemory8(arm_addr, value);
    }
}

// WASM imports use full i32 parameters; narrow inside C++, never in the caller ABI.
// Internal imports preserve the direct-memory callback boundary where needed.
template<bool Experimental=false> static std::uint32_t raw_read32(ARMul_State *s, std::uint32_t a) { memory_callback_scope<Experimental> memory(s); publish_callback_cpsr(s); return s->ReadMemory32(a); }
template<bool Experimental=false> static std::uint32_t raw_read16(ARMul_State *s, std::uint32_t a) { memory_callback_scope<Experimental> memory(s); publish_callback_cpsr(s); return s->ReadMemory16(a); }
template<bool Experimental=false> static std::uint32_t raw_read8(ARMul_State *s, std::uint32_t a) { memory_callback_scope<Experimental> memory(s); publish_callback_cpsr(s); return s->ReadMemory8(a); }
template<bool Experimental=false> static void raw_write32(ARMul_State *s, std::uint32_t a, std::uint32_t v) { memory_callback_scope<Experimental> memory(s); publish_callback_cpsr(s); s->WriteMemory32(a, v); }
template<bool Experimental=false> static void raw_write16(ARMul_State *s, std::uint32_t a, std::uint32_t v) { memory_callback_scope<Experimental> memory(s); publish_callback_cpsr(s); s->WriteMemory16(a, v); }
template<bool Experimental=false> static void raw_write8(ARMul_State *s, std::uint32_t a, std::uint32_t v) { memory_callback_scope<Experimental> memory(s); publish_callback_cpsr(s); s->WriteMemory8(a, v); }

template<unsigned Index> static void count_memory() {
    if (common::performance::counting()) ++common::guest_profile::state.memory_calls[Index];
}
template<bool Experimental> static std::uint32_t prof_read32(ARMul_State *s, std::uint32_t a) { count_memory<0>(); return raw_read32<Experimental>(s,a); }
template<bool Experimental> static void prof_write32(ARMul_State *s, std::uint32_t a, std::uint32_t v) { count_memory<1>(); raw_write32<Experimental>(s,a,v); }
template<bool Experimental> static std::uint32_t prof_read8(ARMul_State *s, std::uint32_t a) { count_memory<2>(); return raw_read8<Experimental>(s,a); }
template<bool Experimental> static void prof_write8(ARMul_State *s, std::uint32_t a, std::uint32_t v) { count_memory<3>(); raw_write8<Experimental>(s,a,v); }
template<bool Experimental> static std::uint32_t prof_read16(ARMul_State *s, std::uint32_t a) { count_memory<4>(); return raw_read16<Experimental>(s,a); }
template<bool Experimental> static void prof_write16(ARMul_State *s, std::uint32_t a, std::uint32_t v) { count_memory<5>(); raw_write16<Experimental>(s,a,v); }

template<bool Experimental=false> static void raw_arm_exclusive(ARMul_State *s, std::uint32_t instruction) {
    memory_callback_scope<Experimental> memory(s);
    // DynCom's exclusive path calls the monitor directly: it does not repack
    // CPSR before the callback. The generated barrier publishes registers and
    // split flags, but must retain that existing packed-CPSR visibility.
    const auto address = s->Reg[(instruction >> 16) & 15];
    const auto destination = (instruction >> 12) & 15;
    if (instruction & (1u << 20))
        s->Reg[destination] = s->exmonitor()->exclusive_read32(s->parent(), address);
    else {
        const auto value = s->Reg[instruction & 15];
        s->Reg[destination] = s->exmonitor()->exclusive_write32(s->parent(), address, value) ? 0 : 1;
    }
    // DynCom advances the callback-visible PC after completing the operation.
    s->Reg[15] += 4;
}

// JS function that instantiates a WASM module and returns exported function
// addresses as a comma-separated string of "name:table_idx" pairs.
// Returns empty string on failure.
EM_JS(char*, js_instantiate_aot_module, (const uint8_t* bytes, int len, const std::uintptr_t *helpers), {
    try {
        var wasmBytes = new Uint8Array(wasmMemory.buffer, bytes, len);
        // Copy the bytes — the buffer may be detached during instantiation
        wasmBytes = new Uint8Array(wasmBytes);
        var wasmModule = new WebAssembly.Module(wasmBytes);

        // wasmExports AND wasmTable.get can be JS abort wrappers. Native table
        // access obtains the C++ function pointers as genuine WASM functions.
        // Outer export/thread-entry abort handling remains enabled unchanged.
        var raw = index => WebAssembly.Table.prototype.get.call(wasmTable, HEAPU32[(helpers >>> 2) + index]);
        var importObj = {env: {
            memory: wasmMemory,
            tlb_read32: raw(0), tlb_write32: raw(1),
            tlb_read8: raw(2), tlb_write8: raw(3),
            tlb_read16: raw(4), tlb_write16: raw(5), arm_exclusive: raw(6)
        }};

        var instance = new WebAssembly.Instance(wasmModule, importObj);

        // Collect exports and add them to Emscripten's function table
        var results = [];
        for (var name in instance.exports) {
            if (name === 'memory') continue;
            var func = instance.exports[name];
            if (typeof func !== 'function') continue;

            // Add to Emscripten's indirect function table
            var tableIdx = addFunction(func, 'ii');
            results.push(name + ':' + tableIdx);
        }

        var resultStr = results.join(',');
        var len = lengthBytesUTF8(resultStr) + 1;
        var ptr = _malloc(len);
        stringToUTF8(resultStr, ptr, len);
        return ptr;
    } catch(e) {
        err('AOT instantiation failed: ' + e.message);
        var ptr = _malloc(1);
        new Uint8Array(wasmMemory.buffer)[ptr] = 0;
        return ptr;
    }
});

// Staged modules waiting for worker-thread instantiation
struct staged_module {
    std::vector<std::uint8_t> wasm_bytes;
    std::string dll_name;
};
static std::vector<staged_module> g_staged_modules;
void stage_aot_module(
    std::vector<std::uint8_t> wasm_bytes,
    const std::string &dll_name)
{
    if (dll_name != "hot-rom") fprintf(stderr, "AOT: staging %zu-byte WASM module for %s\n",
        wasm_bytes.size(), dll_name.c_str());
    g_staged_modules.push_back({std::move(wasm_bytes), dll_name});
}

static int do_instantiate(const std::vector<std::uint8_t> &wasm_bytes,
    const std::string &dll_name)
{
    if (wasm_bytes.empty()) return 0;
    compilation_timer timer{compilation.installation_us};

    const bool experimental = memory_experiment::mode != 0;
    const std::uintptr_t helpers[] = {
        reinterpret_cast<std::uintptr_t>(common::guest_profile::enabled ? (experimental ? prof_read32<true> : prof_read32<false>) : experimental ? raw_read32<true> : raw_read32<false>),
        reinterpret_cast<std::uintptr_t>(common::guest_profile::enabled ? (experimental ? prof_write32<true> : prof_write32<false>) : experimental ? raw_write32<true> : raw_write32<false>),
        reinterpret_cast<std::uintptr_t>(common::guest_profile::enabled ? (experimental ? prof_read8<true> : prof_read8<false>) : experimental ? raw_read8<true> : raw_read8<false>),
        reinterpret_cast<std::uintptr_t>(common::guest_profile::enabled ? (experimental ? prof_write8<true> : prof_write8<false>) : experimental ? raw_write8<true> : raw_write8<false>),
        reinterpret_cast<std::uintptr_t>(common::guest_profile::enabled ? (experimental ? prof_read16<true> : prof_read16<false>) : experimental ? raw_read16<true> : raw_read16<false>),
        reinterpret_cast<std::uintptr_t>(common::guest_profile::enabled ? (experimental ? prof_write16<true> : prof_write16<false>) : experimental ? raw_write16<true> : raw_write16<false>),
        reinterpret_cast<std::uintptr_t>(experimental ? raw_arm_exclusive<true> : raw_arm_exclusive<false>)
    };
    char *result_str = js_instantiate_aot_module(wasm_bytes.data(),
        static_cast<int>(wasm_bytes.size()), helpers);

    if (!result_str || result_str[0] == '\0') {
        if (result_str) free(result_str);
        LOG_ERROR(CPU_DYNCOM, "AOT: failed to instantiate WASM module for {}", dll_name);
        return 0;
    }

    // Parse "f_12345:42,f_67890:43,..."
    std::string results(result_str);
    free(result_str);

    int count = 0;
    registry &reg = global_registry();

    std::size_t pos = 0;
    while (pos < results.size()) {
        std::size_t comma = results.find(',', pos);
        if (comma == std::string::npos) comma = results.size();

        std::string entry = results.substr(pos, comma - pos);
        pos = comma + 1;

        std::size_t colon = entry.find(':');
        if (colon == std::string::npos) continue;

        std::string name = entry.substr(0, colon);
        int table_idx = std::atoi(entry.substr(colon + 1).c_str());

        // Parse address from "f_<decimal_addr>"
        if (name.substr(0, 2) == "r_") {
            ram_cache.attach(static_cast<std::uint32_t>(std::stoul(name.substr(2))),
                reinterpret_cast<aot_func>(table_idx));
            ++count;
            continue;
        }
        if (name.substr(0, 2) != "f_") continue;
        std::uint32_t addr = static_cast<std::uint32_t>(std::stoul(name.substr(2)));

        // Convert table index to function pointer
        auto func = reinterpret_cast<aot_func>(table_idx);
        reg.register_function(addr, func);
        count++;

    }

    completed_function_count.fetch_add(count, std::memory_order_relaxed);
    if (dll_name != "hot-rom") LOG_INFO(CPU_DYNCOM, "AOT: instantiated {} functions for {}", count, dll_name);
    return count;
}

bool instantiate_staged_modules() {
    if (g_staged_modules.empty()) return false;

    for (auto &mod : g_staged_modules) {
        do_instantiate(mod.wasm_bytes, mod.dll_name);
    }
    g_staged_modules.clear();
    return true;
}

#else

void stage_aot_module(
    std::vector<std::uint8_t>,
    const std::string &)
{
}

bool instantiate_staged_modules() {
    return false;
}

#endif // __EMSCRIPTEN__

}
