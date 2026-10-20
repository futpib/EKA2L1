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
#include <cpu/aot/hot_entries.h>
#include <cpu/aot/module_residency.h>
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
#include <fstream>
#include <xxhash.h>
#include <zlib.h>

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
    std::atomic<std::uint64_t> *domain = nullptr;
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    ~compilation_timer() {
        const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - start).count();
        total.fetch_add(elapsed, std::memory_order_relaxed);
        if (domain) domain->fetch_add(elapsed, std::memory_order_relaxed);
    }
};
static std::atomic<std::uint64_t> completed_function_count{0};
static std::atomic<std::uint64_t> retired_function_count{0};
std::uint64_t compiled_function_count() { return completed_function_count.load(std::memory_order_relaxed); }
std::uint64_t retired_compiled_function_count() { return retired_function_count.load(std::memory_order_relaxed); }

static std::uint32_t hot_rom_base = 0, hot_rom_size = 0;
common::diagnostics::flag diagnostics_enabled = false;

// Extend eager compilation using deterministic dispatch samples. ROM bytes
// are immutable; RAM translations follow the selected code-cache policy.
bool hot_compilation_enabled = false;
static validated_code_cache ram_cache;
static validated_code_cache single_cache;
static module_residency resident_modules;
static void trim_compiled_modules();
static bool record_module_usage = false;
static double last_module_install_ms = 0;
static std::uint32_t ram_hot_versions = 0;
static std::unordered_map<std::uint64_t, unsigned> ram_counts;
static const std::uint8_t *hot_rom = nullptr;
static std::uint64_t hot_dispatches = 0;
static std::uint32_t hot_compiled = 0;
static rom_entry_counts hot_counts;
static std::vector<wasm_func_def> hot_pending;
static std::map<std::uint64_t, std::vector<wasm_func_def>> cold_modules;
static constexpr std::uint64_t rom_lifetime = UINT32_MAX;
static std::vector<std::uint32_t> retired_processes;
static std::atomic<bool> process_retirement_pending{false};
struct precompile_image {
    std::uint32_t space, base, size;
    std::vector<std::uint32_t> entries, flat_entries;
};
static std::mutex precompile_mutex;
static std::vector<precompile_image> precompile_images;
static std::atomic<bool> precompile_pending{false};
static std::string rom_cache_path, rom_cache_build, rom_cache_export_path;
static std::atomic<int> rom_cache_state{0};
#ifdef __EMSCRIPTEN__
static void release_retired_functions();
static void merge_cold_modules();
static void release_process_code();
static bool load_rom_cache(const std::string &path);
static bool export_rom_cache(const std::string &path);
#endif

bool configure_rom_cache(const std::string &path, const std::string &build_id) {
    if (build_id.size() != 64 || std::any_of(build_id.begin(), build_id.end(),
            [](char c) { return !(c >= '0' && c <= '9') && !(c >= 'a' && c <= 'f'); })) return false;
    const std::lock_guard<std::mutex> lock(precompile_mutex);
    rom_cache_path = path; rom_cache_build = build_id;
    return true;
}

bool request_rom_cache_export(const std::string &path) {
    const std::lock_guard<std::mutex> lock(precompile_mutex);
    if (!hot_compilation_enabled || path.empty() || rom_cache_build.empty() || rom_cache_state == 1) return false;
    rom_cache_export_path = path;
    rom_cache_state = 1;
    precompile_pending.store(true, std::memory_order_release);
    return true;
}

int rom_cache_status() { return rom_cache_state.load(std::memory_order_acquire); }

void queue_precompile_image(std::uint32_t space, std::uint32_t base,
        const std::uint8_t *bytes, std::uint32_t size, std::vector<std::uint32_t> entries,
        std::vector<std::uint32_t> flat_entries) {
#ifdef __EMSCRIPTEN__
    if (!hot_compilation_enabled || !bytes || !size || std::uint64_t(base) + size > 0x100000000ull) return;
    // Start from loader-provided entries. Scanning instruction words as
    // addresses eagerly compiles unrelated ROM/code destinations.
    const std::lock_guard<std::mutex> lock(precompile_mutex);
    precompile_images.push_back({space, base, size, std::move(entries), std::move(flat_entries)});
    precompile_pending.store(true, std::memory_order_release);
#endif
}

static void flush_hot_blocks() {
    if (hot_pending.empty()) return;
    std::vector<wasm_import_func> imports{{"env","tlb_read32",2,true},{"env","tlb_write32",3,false},
        {"env","tlb_read8",2,true},{"env","tlb_write8",3,false},
        {"env","tlb_read16",2,true},{"env","tlb_write16",3,false},
        {"env","exclusive_read32",2,true},{"env","exclusive_write32",3,true}};
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
    global_registry().configure_rom_index(base, size, true);
    hot_compilation_enabled = enabled;
    completed_function_count = 0;
    retired_function_count = 0;
    compilation.translation_us = 0; compilation.emission_us = 0; compilation.installation_us = 0;
    compilation.eager_functions = 0; compilation.eager_passes = 0;
    compilation.shared_functions = 0; compilation.demand_functions = 0; compilation.single_functions = 0;
    compilation.rom_translation_us = 0; compilation.ram_translation_us = 0;
    compilation.rom_translations = 0; compilation.ram_translations = 0;
    { const std::lock_guard<std::mutex> lock(precompile_mutex);
      precompile_images.clear(); precompile_pending = !rom_cache_path.empty();
      rom_cache_export_path.clear(); rom_cache_state = rom_cache_path.empty() ? 0 : 1;
      retired_processes.clear(); process_retirement_pending = false; }
    hot_dispatches = 0; hot_compiled = 0; hot_counts.clear(); hot_pending.clear(); cold_modules.clear();
    ram_cache = {}; single_cache = {}; ram_hot_versions = 0; ram_counts.clear();
    resident_modules = {};
}

void invalidate_ram_code(std::uint32_t address, std::size_t size) {
    ram_cache.invalidate(address, size);
    single_cache.invalidate(address, size);
}

void retire_process_code(std::uint32_t address_space) {
#ifdef __EMSCRIPTEN__
    const std::lock_guard<std::mutex> lock(precompile_mutex);
    retired_processes.push_back(address_space);
    process_retirement_pending.store(true, std::memory_order_release);
#endif
}

template<bool Profile, bool TrustBytes = false>
#if defined(_MSC_VER)
__forceinline
#else
__attribute__((always_inline))
#endif
static aot_func lookup_compiled_impl(ARMul_State *cpu, const registry &functions) {
    // TrustBytes follows the frozen code-cache policy.
    const auto pc = cpu->Reg[15], pc_mode = pc | cpu->TFlag;
    // Existing ROM functions use immutable bytes and need no mapping lookup.
    if (!hot_compilation_enabled || (pc >= hot_rom_base && pc - hot_rom_base < hot_rom_size)) {
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
    if constexpr(!TrustBytes) {
        cpu->aot_code_begin = static_cast<std::uint32_t>(entry->guard_begin);
        cpu->aot_code_end = static_cast<std::uint32_t>(entry->guard_end);
    }
    return entry->function;
}

aot_func lookup_compiled(ARMul_State *cpu) {
    if (common::guest_profile::enabled && common::performance::enabled && common::performance::detailed)
        return lookup_compiled_impl<true>(cpu, global_registry());
    if (common::code_tracking::skip_code_scans())
        return lookup_compiled_impl<false, true>(cpu, global_registry());
    return lookup_compiled_impl<false>(cpu, global_registry());
}

template<bool TrustBytes, bool Direct, bool RecordUsage = false>
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
        if constexpr (RecordUsage)
            resident_modules.touch(reinterpret_cast<std::uintptr_t>(function));
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
    if (record_module_usage) {
        if (common::code_tracking::skip_code_scans())
            return execute_chain_impl<true, Direct, true>(cpu, function);
        return execute_chain_impl<false, Direct, true>(cpu, function);
    }
    if (common::code_tracking::skip_code_scans())
        return execute_chain_impl<true, Direct>(cpu, function);
    return execute_chain_impl<false, Direct>(cpu, function);
}

void finish_compiled_batch(double now_ms) {
    // Usage is needed while the cache is changing. Once compilation settles,
    // select the original inner runner without usage stores.
    if (now_ms - last_module_install_ms > 20) record_module_usage = false;
}

compiled_run execute_chain(ARMul_State *cpu, aot_func function) {
    return memory_experiment::enabled() ? execute_chain_selected<true>(cpu,function) : execute_chain_selected<false>(cpu,function);
}

std::uint32_t execute_single(ARMul_State *cpu, aot_func function) {
    resident_modules.touch(reinterpret_cast<std::uintptr_t>(function));
    cpu->aot_exit = 0;
    return function(cpu);
}

#ifdef __EMSCRIPTEN__
static void discover_successors(core &cpu, const translate_result &tr, bool thumb,
        std::vector<std::uint32_t> &successors, bool include_literals = true) {
    successors.insert(successors.end(), tr.dispatch_entries.begin(), tr.dispatch_entries.end());
    for (auto address : tr.resume_points) successors.push_back(address | (thumb ? 1u : 0u));
    if (!include_literals) return;
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

static unsigned compilation_policy() {
    return memory_experiment::mode | (common::code_tracking::unsafe_code_mode << 4);
}

static void remember_cold_function(const wasm_func_def &function, const core &cpu, std::uint32_t pc) {
    std::uint64_t lifetime = rom_lifetime;
    if (pc - hot_rom_base >= hot_rom_size) {
        std::uint32_t base = pc & ~4095u;
        const std::lock_guard<std::mutex> lock(precompile_mutex);
        for (const auto &image : precompile_images)
            if (image.space == cpu.code_address_space && pc - image.base < image.size) { base = image.base; break; }
        lifetime = validated_code_cache::key(cpu.code_address_space, base);
    }
    cold_modules[lifetime].push_back(function);
}

static void compile_at(core &cpu, std::uint32_t key, std::vector<std::uint32_t> *successors = nullptr, bool demand = false) {
    const bool immediate = successors || demand;
    const auto pc = key & ~1u;
    const bool thumb = key & 1;
    if (pc < hot_rom_base || pc - hot_rom_base >= hot_rom_size) {
        if (!hot_compilation_enabled || !cpu.resolve_code) return;
        // As for ROM, preparation of known loaded images does not consume the
        // hot-discovery quota. A launcher must not exhaust it before the game
        // loads and leave every later RAM image permanently interpreted.
        if (!immediate && ram_hot_versions >= 16384) {
            if ((common::guest_profile::enabled && common::performance::counting())) common::guest_profile::state.event("ram_capacity",key);
            return;
        }
        core::code_mapping view;
        if (!cpu.resolve_code(pc, view)) return;
        if (ram_cache.find(key, cpu)) return; // compiled or awaiting instantiation
        if (ram_cache.share(key, view, cpu, compilation_policy())) {
            ++compilation.shared_functions;
            return;
        }
        if (!immediate) {
            const auto identity = validated_code_cache::key(view.address_space, key);
            if (ram_counts.size() >= 131072 && !ram_counts.count(identity)) return;
            auto &count = ram_counts[identity];
            if (++count % 8) {
                if ((common::guest_profile::enabled && common::performance::counting())) common::guest_profile::state.event("candidate_threshold",key,view.address_space);
                return;
            }
        }
        const auto size = std::min(std::size_t(primary_window_bytes), view.size);
        leaf_resolver leaves = [&](std::uint32_t target) {
            core::code_mapping leaf;
            if (!cpu.resolve_code(target, leaf) || leaf.address_space != view.address_space)
                return std::vector<std::uint8_t>{};
            const auto bytes = std::min(std::size_t(arm_indirect_calls ? primary_window_bytes : leaf_instruction_limit*4), leaf.size);
            return std::vector<std::uint8_t>(leaf.bytes, leaf.bytes + bytes);
        };
        const code_window immutable_code{hot_rom, hot_rom_base, hot_rom_size};
        auto translate = [&] {return thumb ? translate_thumb_block(view.bytes, size, pc, nullptr, nullptr, true, true, true, &immutable_code, &leaves)
                            : translate_arm_block(view.bytes, size, pc, nullptr, nullptr, true, true, true, true, &leaves, true, arm_ir_policy::write_spans);};
        auto tr = [&] { compilation_timer timer{compilation.translation_us, &compilation.ram_translation_us}; return translate(); }();
        if (tr.func.body.empty() || !tr.entry_supported) {
            // Cache rejection against these exact bytes; retry only after mutation.
            ram_cache.insert(key, view, std::min(size, std::size_t(thumb ? 2 : 4)), compilation_policy()).rejected = true;
            if (!immediate) ++ram_hot_versions;
            if ((common::guest_profile::enabled && common::performance::counting())) common::guest_profile::state.event("compile_rejected",key,view.address_space);
            return;
        }
        ++compilation.ram_translations;
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
        auto &entry = ram_cache.insert(key, view, consumed, compilation_policy());
        if (!immediate) ++ram_hot_versions;
        for (const auto &dependency : tr.dependencies) {
            core::code_mapping leaf;
            if (!cpu.resolve_code(dependency.address, leaf)
                || leaf.address_space != view.address_space || leaf.size < dependency.bytes.size()
                || (!common::code_tracking::skip_code_scans()
                    && !equal_code_bytes(leaf.bytes, dependency.bytes.data(), dependency.bytes.size()))) {
                entry.live = false;
                return;
            }
            ram_cache.add_dependency(entry, dependency.address, leaf.bytes, dependency.bytes);
        }
        tr.func.export_name = "r_" + std::to_string(entry.version) + "_pc_" + std::to_string(pc);
        if (successors) {
            discover_successors(cpu, tr, thumb, *successors, !demand);
            if (!demand) ++compilation.eager_functions;
        }
        if (demand) { ++compilation.demand_functions; remember_cold_function(tr.func, cpu, pc); }
        hot_pending.push_back(std::move(tr.func));
        if (common::performance::counting()) ++common::performance::ram_blocks_compiled;
        if (hot_pending.size() >= (demand ? 16u : 32u)) flush_hot_blocks();
        return;
    }
    if (global_registry().lookup(key)) return;
    if (!immediate && hot_compiled >= 4096) return;
    if (!hot_counts.ready(key, immediate)) return;
    const auto offset = pc - hot_rom_base;
    const auto size = std::min(primary_window_bytes, hot_rom_size - offset);
    const code_window immutable_code{hot_rom, hot_rom_base, hot_rom_size};
    auto translate = [&] {return thumb ? translate_thumb_block(hot_rom + offset, size, pc, nullptr, nullptr, true, false, true, &immutable_code, nullptr)
                        : translate_arm_block(hot_rom + offset, size, pc, nullptr, nullptr, true, false, true, true, nullptr, true, arm_ir_policy::write_spans);};
    auto tr = [&] { compilation_timer timer{compilation.translation_us, &compilation.rom_translation_us}; return translate(); }();
    if (tr.func.body.empty() || !tr.entry_supported) return;
    ++compilation.rom_translations;
    tr.func.export_name = "f_" + std::to_string(key);
    if (successors) {
        discover_successors(cpu, tr, thumb, *successors, !demand);
        if (!demand) ++compilation.eager_functions;
    }
    if (demand) {
        ++compilation.demand_functions; remember_cold_function(tr.func, cpu, pc);
    }
    hot_pending.push_back(std::move(tr.func));
    if (hot_pending.size() >= (demand ? 16u : 32u)) flush_hot_blocks();
    if (!immediate) ++hot_compiled;
}
#endif

void prepare_compiled_code(core &cpu) {
#ifdef __EMSCRIPTEN__
    if (process_retirement_pending.load(std::memory_order_acquire)) release_process_code();
    // Previous CPU runs have returned every generated frame. Unmap callbacks
    // only queued retirement, so no table slot is reused while it is executing.
    if (ram_cache.has_retired() || single_cache.has_retired()) release_retired_functions();
    trim_compiled_modules();
    if (!hot_compilation_enabled || !precompile_pending.load(std::memory_order_acquire)) return;
    std::vector<precompile_image> images;
    std::string cache_path, export_path;
    {
        const std::lock_guard<std::mutex> lock(precompile_mutex);
        cache_path.swap(rom_cache_path); export_path.swap(rom_cache_export_path);
        for (auto &image : precompile_images) {
            if (image.space == cpu.code_address_space || image.base - hot_rom_base < hot_rom_size) {
                images.push_back({image.space, image.base, image.size, std::move(image.entries), std::move(image.flat_entries)});
            }
        }
        precompile_pending = std::any_of(precompile_images.begin(), precompile_images.end(),
            [](const auto &image) { return !image.entries.empty() || !image.flat_entries.empty(); });
    }
    if (!cache_path.empty()) {
        const bool loaded = load_rom_cache(cache_path);
        rom_cache_state.store(loaded ? 2 : -1, std::memory_order_release);
        // Launcher uploads are temporary; native modules own their code now.
        std::remove(cache_path.c_str());
    }
    if (!export_path.empty()) rom_cache_state.store(export_rom_cache(export_path) ? 2 : -1, std::memory_order_release);
    std::vector<std::uint32_t> pending, flat;
    for (auto &image : images) {
        pending.insert(pending.end(), image.entries.begin(), image.entries.end());
        flat.insert(flat.end(), image.flat_entries.begin(), image.flat_entries.end());
    }
    if (pending.empty() && flat.empty()) return;
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
    // Address-taken callbacks are known entries, not evidence that all their
    // out-of-line continuations run during this startup.
    std::vector<std::uint32_t> ignored;
    for (const auto key : flat) {
        const auto pc = key & ~1u;
        if ((!((key & 1) || !(pc & 3))) || !visited.insert(key).second) continue;
        if (std::none_of(images.begin(), images.end(), [&](const auto &image) { return pc - image.base < image.size; })) continue;
        ignored.clear();
        compile_at(cpu, key, &ignored);
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

aot_func compile_missing(ARMul_State *cpu) {
#ifdef __EMSCRIPTEN__
    flush_hot_blocks();
    trim_compiled_modules();
    const auto root = cpu->Reg[15] | cpu->TFlag;
    const bool rom = cpu->Reg[15] - hot_rom_base < hot_rom_size;
    std::vector<std::pair<std::uint32_t, std::uint32_t>> ranges;
    {
        const std::lock_guard<std::mutex> lock(precompile_mutex);
        for (const auto &image : precompile_images) {
            const bool image_rom = image.base - hot_rom_base < hot_rom_size;
            if ((rom && image_rom) || (!rom && !image_rom && image.space == cpu->parent()->code_address_space
                    && cpu->Reg[15] - image.base < image.size)) ranges.emplace_back(image.base, image.size);
        }
    }
    // Follow only static edges through initialized code with the same lifetime.
    // ROM groups are permanent; a RAM batch belongs to its loaded image.
    std::vector<std::uint32_t> pending{root};
    std::unordered_set<std::uint32_t> visited;
    for (std::size_t i = 0; i < pending.size() && visited.size() < 4; ++i) {
        const auto key = pending[i], pc = key & ~1u;
        if (!(key & 1) && (pc & 3)) continue;
        if (key != root && std::none_of(ranges.begin(), ranges.end(),
                [&](const auto &range) { return pc - range.first < range.second; })) continue;
        if (!visited.insert(key).second) continue;
        compile_at(*cpu->parent(), key, &pending, true);
    }
    flush_hot_blocks();
    merge_cold_modules();
#endif
    return lookup_compiled(cpu);
}

aot_func compile_single_instruction(ARMul_State *cpu, int decode_thumb) {
#ifdef __EMSCRIPTEN__
    const auto thumb = decode_thumb < 0 ? cpu->TFlag : static_cast<unsigned>(decode_thumb);
    const auto key = cpu->Reg[15] | thumb;
    if (auto *entry = single_cache.find(key, *cpu->parent())) return entry->function;
    core::code_mapping view;
    if (!cpu->parent()->resolve_code || !cpu->parent()->resolve_code(cpu->Reg[15], view)) return nullptr;
    if (auto function = single_cache.share(key, view, *cpu->parent(), compilation_policy())) {
        ++compilation.shared_functions;
        return function;
    }
    const auto size = std::min<std::size_t>(view.size, thumb ? 2 : 4);
    auto tr = thumb ? translate_thumb_block(view.bytes,size,cpu->Reg[15],nullptr,nullptr,true,false,false)
                        : translate_arm_block(view.bytes,size,cpu->Reg[15],nullptr,nullptr,true,false,false,false);
    if (!tr.entry_supported || tr.func.body.empty()) return nullptr;
    auto &entry = single_cache.insert(key,view,size,compilation_policy());
    tr.func.export_name = "s_" + std::to_string(entry.version);
    ++compilation.single_functions;
    hot_pending.push_back(std::move(tr.func));
    flush_hot_blocks();
    return entry.function;
#else
    return nullptr;
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

template<bool Direct> static std::uint32_t raw_exclusive_read32(ARMul_State *s, std::uint32_t address) {
    memory_callback_scope<Direct> memory(s);
    return s->exmonitor()->exclusive_read32(s->parent(), address);
}
template<bool Direct> static std::uint32_t raw_exclusive_write32(ARMul_State *s, std::uint32_t address, std::uint32_t value) {
    memory_callback_scope<Direct> memory(s);
    return s->exmonitor()->exclusive_write32(s->parent(), address, value) ? 0 : 1;
}

template<unsigned Index> static void count_memory() {
    if (common::performance::counting()) ++common::guest_profile::state.memory_calls[Index];
}
template<bool Experimental> static std::uint32_t prof_read32(ARMul_State *s, std::uint32_t a) { count_memory<0>(); return raw_read32<Experimental>(s,a); }
template<bool Experimental> static void prof_write32(ARMul_State *s, std::uint32_t a, std::uint32_t v) { count_memory<1>(); raw_write32<Experimental>(s,a,v); }
template<bool Experimental> static std::uint32_t prof_read8(ARMul_State *s, std::uint32_t a) { count_memory<2>(); return raw_read8<Experimental>(s,a); }
template<bool Experimental> static void prof_write8(ARMul_State *s, std::uint32_t a, std::uint32_t v) { count_memory<3>(); raw_write8<Experimental>(s,a,v); }
template<bool Experimental> static std::uint32_t prof_read16(ARMul_State *s, std::uint32_t a) { count_memory<4>(); return raw_read16<Experimental>(s,a); }
template<bool Experimental> static void prof_write16(ARMul_State *s, std::uint32_t a, std::uint32_t v) { count_memory<5>(); raw_write16<Experimental>(s,a,v); }

// JS function that instantiates a WASM module and returns exported function
// addresses as a comma-separated string of "name:table_idx" pairs.
// Returns empty string on failure.
EM_JS(char*, js_instantiate_aot_module, (const uint8_t* bytes, int len, const std::uintptr_t *helpers, const std::uintptr_t *slots), {
    try {
        var wasmBytes = new Uint8Array(wasmMemory.buffer, bytes, len);
        var wasmModule = new WebAssembly.Module(wasmBytes);

        // wasmExports AND wasmTable.get can be JS abort wrappers. Native table
        // access obtains the C++ function pointers as genuine WASM functions.
        // Outer export/thread-entry abort handling remains enabled unchanged.
        var raw = index => WebAssembly.Table.prototype.get.call(wasmTable, HEAPU32[(helpers >>> 2) + index]);
        var importObj = {env: {
            memory: wasmMemory,
            tlb_read32: raw(0), tlb_write32: raw(1),
            tlb_read8: raw(2), tlb_write8: raw(3),
            tlb_read16: raw(4), tlb_write16: raw(5),
            exclusive_read32: raw(6), exclusive_write32: raw(7)
        }};

        var instance = new WebAssembly.Instance(wasmModule, importObj);

        // Collect exports and add them to Emscripten's function table
        var results = [];
        var exportIndex = 0;
        for (var name in instance.exports) {
            if (name === 'memory') continue;
            var func = instance.exports[name];
            if (typeof func !== 'function') continue;

            // Add to Emscripten's indirect function table
            var tableIdx;
            if (slots) {
                tableIdx = HEAPU32[(slots >>> 2) + exportIndex++];
                setWasmTableEntry(tableIdx, func);
            } else tableIdx = addFunction(func, 'ii');
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

EM_JS(void, js_release_aot_functions, (const std::uintptr_t *functions, std::size_t count), {
    for (var i = 0; i < count; ++i) {
        var index = HEAPU32[(functions >>> 2) + i];
        removeFunction(index);
    }
});

static void release_functions(const std::vector<aot_func> &functions) {
    if (functions.empty()) return;
    static_assert(sizeof(aot_func) == sizeof(std::uintptr_t));
    js_release_aot_functions(reinterpret_cast<const std::uintptr_t *>(functions.data()), functions.size());
    for (auto function : functions)
        resident_modules.release(reinterpret_cast<std::uintptr_t>(function));
    retired_function_count.fetch_add(functions.size(), std::memory_order_relaxed);
}

static void release_retired_functions() {
    release_functions(ram_cache.collect_retired());
    release_functions(single_cache.collect_retired());
    for (auto it = cold_modules.begin(); it != cold_modules.end();) {
        auto &pending = it->second;
        pending.erase(std::remove_if(pending.begin(), pending.end(), [](const auto &function) {
            const auto key = static_cast<std::uint32_t>(std::stoul(function.export_name.substr(2)));
            return function.export_name[0] == 'r' ? !ram_cache.function_for_version(key)
                : !global_registry().lookup(key);
        }), pending.end());
        if (pending.empty()) it = cold_modules.erase(it);
        else ++it;
    }
}

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
    const std::string &dll_name, const std::vector<std::uintptr_t> *slots = nullptr)
{
    if (wasm_bytes.empty()) return 0;
    record_module_usage = true;
    compilation_timer timer{compilation.installation_us};

    const bool experimental = memory_experiment::mode != 0;
    const std::uintptr_t helpers[] = {
        reinterpret_cast<std::uintptr_t>(common::guest_profile::enabled ? (experimental ? prof_read32<true> : prof_read32<false>) : experimental ? raw_read32<true> : raw_read32<false>),
        reinterpret_cast<std::uintptr_t>(common::guest_profile::enabled ? (experimental ? prof_write32<true> : prof_write32<false>) : experimental ? raw_write32<true> : raw_write32<false>),
        reinterpret_cast<std::uintptr_t>(common::guest_profile::enabled ? (experimental ? prof_read8<true> : prof_read8<false>) : experimental ? raw_read8<true> : raw_read8<false>),
        reinterpret_cast<std::uintptr_t>(common::guest_profile::enabled ? (experimental ? prof_write8<true> : prof_write8<false>) : experimental ? raw_write8<true> : raw_write8<false>),
        reinterpret_cast<std::uintptr_t>(common::guest_profile::enabled ? (experimental ? prof_read16<true> : prof_read16<false>) : experimental ? raw_read16<true> : raw_read16<false>),
        reinterpret_cast<std::uintptr_t>(common::guest_profile::enabled ? (experimental ? prof_write16<true> : prof_write16<false>) : experimental ? raw_write16<true> : raw_write16<false>),
        reinterpret_cast<std::uintptr_t>(experimental ? raw_exclusive_read32<true> : raw_exclusive_read32<false>),
        reinterpret_cast<std::uintptr_t>(experimental ? raw_exclusive_write32<true> : raw_exclusive_write32<false>)
    };
    char *result_str = js_instantiate_aot_module(wasm_bytes.data(),
        static_cast<int>(wasm_bytes.size()), helpers, slots ? slots->data() : nullptr);
    last_module_install_ms = emscripten_get_now();

    if (!result_str || result_str[0] == '\0') {
        if (result_str) free(result_str);
        LOG_ERROR(CPU_DYNCOM, "AOT: failed to instantiate WASM module for {}", dll_name);
        return 0;
    }

    if (slots) {
        resident_modules.install(wasm_bytes.size(), *slots);
        free(result_str);
        return 0;
    }

    // Parse "f_12345:42,f_67890:43,..."
    std::string results(result_str);
    free(result_str);

    int count = 0;
    std::vector<aot_func> rejected;
    std::vector<std::uintptr_t> installed_slots;
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
        installed_slots.push_back(table_idx);

        // Parse address from "f_<decimal_addr>"
        if (name.substr(0, 2) == "r_" || name.substr(0, 2) == "s_") {
            const auto function = reinterpret_cast<aot_func>(table_idx);
            const auto version = static_cast<std::uint32_t>(std::stoul(name.substr(2)));
            auto &cache = name[0] == 'r' ? ram_cache : single_cache;
            if (!cache.attach(version, function) && cache.was_unloaded(version))
                rejected.push_back(function);
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
    resident_modules.install(wasm_bytes.size(), installed_slots);
    release_functions(rejected);
    if (dll_name != "hot-rom" && dll_name != "rom-cache") LOG_INFO(CPU_DYNCOM, "AOT: instantiated {} functions for {}", count, dll_name);
    return count;
}

static void trim_compiled_modules() {
    constexpr std::size_t limit = 32u * 1024 * 1024;
    if (resident_modules.bytes() <= limit) return;
    evict_compiled_modules(limit * 3 / 4);
}

void evict_compiled_modules(std::size_t target_bytes) {
    // Only called after generated frames returned. Retire all guest owners
    // before recycling table slots, including cross-process shared functions.
    const auto slots = resident_modules.victims(target_bytes);
    std::unordered_set<aot_func> selected;
    for (auto slot : slots) selected.insert(reinterpret_cast<aot_func>(slot));
    std::vector<aot_func> rom_functions;
    auto &registry = global_registry();
    for (auto pc : registry.addresses()) {
        const auto function = registry.lookup(pc);
        if (!selected.count(function)) continue;
        registry.unregister_function(pc);
        hot_counts.forget(pc);
        rom_functions.push_back(function);
    }
    const auto matches = [&](aot_func function) { return selected.count(function); };
    ram_cache.retire_functions(matches);
    single_cache.retire_functions(matches);
    release_functions(rom_functions);
    release_retired_functions();
    fprintf(stderr, "AOT: evicted %zu cold functions; retained %.1f MiB module bytes\n",
        slots.size(), resident_modules.bytes() / 1048576.0);
}

static std::array<std::uint64_t, 9> rom_cache_header() {
    return {0x314d4f52414b45ull, 4, hot_rom_base, hot_rom_size,
        XXH64(hot_rom, hot_rom_size, 0), reinterpret_cast<std::uintptr_t>(&watchdog::request),
        compilation_policy(), sizeof(ARMul_State), 64};
}

static bool export_rom_cache(const std::string &path) {
    if (!hot_rom || !hot_rom_size) return false;
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    const auto header = rom_cache_header();
    file.write(reinterpret_cast<const char *>(header.data()), sizeof(header));
    file.write(rom_cache_build.data(), 64);
    const code_window immutable_code{hot_rom, hot_rom_base, hot_rom_size};
    const std::vector<wasm_import_func> imports{{"env","tlb_read32",2,true},{"env","tlb_write32",3,false},
        {"env","tlb_read8",2,true},{"env","tlb_write8",3,false},
        {"env","tlb_read16",2,true},{"env","tlb_write16",3,false},
        {"env","exclusive_read32",2,true},{"env","exclusive_write32",3,true}};
    std::vector<wasm_func_def> functions;
    unsigned count = 0;
    const auto flush = [&] {
        if (functions.empty()) return;
        const auto bytes = build_wasm_module(functions, imports);
        uLongf length = compressBound(bytes.size());
        std::vector<std::uint8_t> compressed(length);
        if (compress2(compressed.data(), &length, bytes.data(), bytes.size(), 1) != Z_OK) {
            file.setstate(std::ios::failbit); return;
        }
        compressed.resize(length);
        const std::uint64_t record[]{compressed.size(), bytes.size(), XXH64(compressed.data(), compressed.size(), 0)};
        file.write(reinterpret_cast<const char *>(record), sizeof(record));
        file.write(reinterpret_cast<const char *>(compressed.data()), compressed.size());
        count += functions.size(); functions.clear();
    };
    for (const auto key : global_registry().addresses()) {
        const auto pc = key & ~1u;
        if (pc - hot_rom_base >= hot_rom_size) continue;
        const auto offset = pc - hot_rom_base;
        const auto size = std::min(primary_window_bytes, hot_rom_size - offset);
        auto tr = key & 1
            ? translate_thumb_block(hot_rom + offset, size, pc, nullptr, nullptr, true, false, true, &immutable_code, nullptr)
            : translate_arm_block(hot_rom + offset, size, pc, nullptr, nullptr, true, false, true, true, nullptr, true, arm_ir_policy::write_spans);
        if (!tr.entry_supported || tr.func.body.empty()) continue;
        tr.func.export_name = "f_" + std::to_string(key);
        functions.push_back(std::move(tr.func));
        if (functions.size() == 64) { flush(); if (!file) return false; }
    }
    flush();
    const std::uint64_t end[]{0, 0, 0};
    file.write(reinterpret_cast<const char *>(end), sizeof(end));
    file.close();
    if (!file || !count) { std::remove(path.c_str()); return false; }
    LOG_INFO(CPU_DYNCOM, "AOT: exported {} immutable ROM functions to {}", count, path);
    return true;
}

static bool load_rom_cache(const std::string &path) {
    if (!hot_rom || !hot_rom_size) return false;
    std::ifstream file(path, std::ios::binary);
    std::array<std::uint64_t, 9> header{};
    std::string build(64, '\0');
    file.read(reinterpret_cast<char *>(header.data()), sizeof(header));
    file.read(build.data(), build.size());
    if (!file || header != rom_cache_header() || build != rom_cache_build) {
        LOG_WARN(CPU_DYNCOM, "AOT: ignored incompatible ROM translation cache {}", path);
        const auto expected = rom_cache_header();
        for (unsigned i = 0; i < header.size(); ++i) if (header[i] != expected[i])
            LOG_WARN(CPU_DYNCOM, "AOT: ROM cache header {} differs ({} vs {})", i, header[i], expected[i]);
        return false;
    }
    const auto begin = file.tellg();
    std::vector<std::uint8_t> bytes;
    // Check all record extents and checksums before installing any functions.
    for (;;) {
        std::uint64_t record[3]{};
        file.read(reinterpret_cast<char *>(record), sizeof(record));
        if (!file || record[0] > 64 * 1024 * 1024 || record[1] > 64 * 1024 * 1024) return false;
        if (!record[0]) {
            if (record[1] || record[2] || file.peek() != std::char_traits<char>::eof()) return false;
            break;
        }
        bytes.resize(record[0]);
        file.read(reinterpret_cast<char *>(bytes.data()), bytes.size());
        if (!file || !record[1] || XXH64(bytes.data(), bytes.size(), 0) != record[2]) return false;
    }
    file.clear(); file.seekg(begin);
    unsigned count = 0;
    for (;;) {
        std::uint64_t record[3]{};
        file.read(reinterpret_cast<char *>(record), sizeof(record));
        if (!record[0]) break;
        bytes.resize(record[0]);
        file.read(reinterpret_cast<char *>(bytes.data()), bytes.size());
        std::vector<std::uint8_t> module(record[1]);
        uLongf length = module.size();
        if (uncompress(module.data(), &length, bytes.data(), bytes.size()) != Z_OK || length != module.size()) return false;
        const auto installed = do_instantiate(module, "rom-cache");
        if (!installed) return false;
        count += installed;
    }
    LOG_INFO(CPU_DYNCOM, "AOT: loaded {} cached ROM functions", count);
    return count != 0;
}

static void release_process_code() {
    std::vector<std::uint32_t> processes;
    {
        const std::lock_guard<std::mutex> lock(precompile_mutex);
        processes.swap(retired_processes);
        process_retirement_pending = false;
        precompile_images.erase(std::remove_if(precompile_images.begin(), precompile_images.end(),
            [&](const auto &image) { return std::find(processes.begin(), processes.end(), image.space) != processes.end(); }), precompile_images.end());
        precompile_pending = std::any_of(precompile_images.begin(), precompile_images.end(), [](const auto &image) { return !image.entries.empty() || !image.flat_entries.empty(); });
    }
    for (const auto space : processes) {
        ram_cache.retire_space(space);
        single_cache.retire_space(space);
        for (auto it = cold_modules.begin(); it != cold_modules.end();)
            if (it->first != rom_lifetime && it->first >> 32 == space) it = cold_modules.erase(it);
            else ++it;
    }
}

static void merge_cold_modules() {
    const std::vector<wasm_import_func> imports{{"env","tlb_read32",2,true},{"env","tlb_write32",3,false},
        {"env","tlb_read8",2,true},{"env","tlb_write8",3,false},
        {"env","tlb_read16",2,true},{"env","tlb_write16",3,false},
        {"env","exclusive_read32",2,true},{"env","exclusive_write32",3,true}};
    for (auto it = cold_modules.begin(); it != cold_modules.end();) {
        auto &pending = it->second;
        if (pending.size() < 64) { ++it; continue; }
        std::vector<wasm_func_def> live;
        std::vector<std::uintptr_t> slots;
        for (auto &function : pending) {
            const auto version = static_cast<std::uint32_t>(std::stoul(function.export_name.substr(2)));
            const auto compiled = function.export_name[0] == 'r' ? ram_cache.function_for_version(version)
                : global_registry().lookup(version);
            if (compiled) {
                slots.push_back(reinterpret_cast<std::uintptr_t>(compiled));
                live.push_back(std::move(function));
            }
        }
        if (!live.empty()) {
            auto bytes = [&] { compilation_timer timer{compilation.emission_us}; return build_wasm_module(live, imports); }();
            // All generated frames have returned. Keep each callable index
            // while replacing independent modules with one lifetime-matched batch.
            do_instantiate(bytes, "cold-merged", &slots);
        }
        it = cold_modules.erase(it);
    }
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

void evict_compiled_modules(std::size_t) {
}

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
