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
#include <cpu/aot/aot_registry.h>
#include <cpu/aot/code_cache.h>
#include <cpu/aot/exit_census.h>
#include <cpu/aot/execution_limits.h>
#include <common/performance.h>
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

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif

namespace eka2l1::arm::aot {
#ifdef __EMSCRIPTEN__
static_assert(offsetof(ARMul_State, NumInstrsToExecute) == state_offsets::NUM_INSTRS_TO_EXECUTE);
#endif
static std::atomic<std::uint64_t> completed_function_count{0};
std::uint64_t compiled_function_count() { return completed_function_count.load(std::memory_order_relaxed); }

// Optional differential execution. Guest memory is changed only by compiled
// execution; the reference interpreter uses a private byte overlay.
static std::uint32_t hot_rom_base = 0, hot_rom_size = 0;
bool rom_inline_leaves = false;
bool rom_bounded_calls = false;
std::vector<std::uint8_t> resolve_rom_leaf(const std::uint8_t *host,
    std::uint32_t base, std::uint32_t size, std::uint32_t target) {
    if (!host || (target & 3) || target < base) return {};
    const auto offset = target - base;
    if (offset >= size) return {};
    // Never cross the supplied image extent or the 32-bit guest address space.
    const auto available = std::min<std::uint64_t>(size - offset,
        (std::uint64_t{1} << 32) - target);
    const auto bytes = std::min<std::uint64_t>(leaf_instruction_limit * 4, available) & ~std::uint64_t{3};
    return {host + offset, host + offset + bytes};
}
bool diagnostics_enabled = false;
bool omit_guard_publication = false;
bool validation_running = false;
static bool validating = false;
static ARMul_State *validation_guest = nullptr;
static core::thread_context validation_before;
static std::unordered_map<std::uint32_t, std::uint8_t> validation_memory;
static std::unordered_map<std::uint32_t, std::uint8_t> reference_writes;

static unsigned verification_stride() {
    static const unsigned stride = [] {
        const char *value = std::getenv("EKA2L1_AOT_VERIFY");
        return value ? std::max(1ul, std::strtoul(value, nullptr, 10)) : 0ul;
    }();
    return stride;
}

static inline void count_ram_dispatch(ARMul_State *cpu) {
    if (common::performance::counting() && ram_compilation_enabled
        && (cpu->Reg[15] < hot_rom_base || cpu->Reg[15] - hot_rom_base >= hot_rom_size))
        ++common::performance::ram_aot_dispatches;
}

void validation_begin(ARMul_State *cpu) {
    const auto stride = verification_stride();
    static std::uint64_t attempts = 0;
    validating = stride && (++attempts % stride == 0);
    if (!validating) return;
    validation_guest = cpu;
    cpu->parent()->save_context(validation_before);
    validation_before.cpsr = (cpu->Cpsr & 0x0fffffdf) | (cpu->NFlag << 31)
        | (cpu->ZFlag << 30) | (cpu->CFlag << 29) | (cpu->VFlag << 28) | (cpu->TFlag << 5);
    validation_memory.clear(); reference_writes.clear();
}

static void validation_access(ARMul_State *cpu, std::uint32_t addr, unsigned size) {
    if (!validating) return;
    for (unsigned i = 0; i < size; ++i)
        if (!validation_memory.count(addr + i)) validation_memory[addr+i] = cpu->ReadMemory8(addr+i);
}

template<typename T> static bool reference_read(std::uint32_t addr, T *value) {
    auto *bytes = reinterpret_cast<std::uint8_t *>(value);
    for (unsigned i = 0; i < sizeof(T); ++i) {
        const auto a = addr + i;
        auto w = reference_writes.find(a);
        auto v = validation_memory.find(a);
        bytes[i] = w != reference_writes.end() ? w->second
            : v != validation_memory.end() ? v->second : validation_guest->ReadMemory8(a);
    }
    return true;
}
template<typename T> static bool reference_write(std::uint32_t addr, T *value) {
    const auto *bytes = reinterpret_cast<std::uint8_t *>(value);
    for (unsigned i = 0; i < sizeof(T); ++i) reference_writes[addr+i] = bytes[i];
    return true;
}

void validation_end(ARMul_State *cpu, std::uint32_t count) {
    if (!validating || !count) { validating = false; return; }
    static r12l1::exclusive_monitor monitor(1);
    static dyncom_core reference(&monitor, 12);
    reference.read_code = [cpu](address addr, std::uint32_t *v) { return cpu->parent()->read_code(addr, v); };
    reference.read_8bit = reference_read<std::uint8_t>; reference.write_8bit = reference_write<std::uint8_t>;
    reference.read_16bit = reference_read<std::uint16_t>; reference.write_16bit = reference_write<std::uint16_t>;
    reference.read_32bit = reference_read<std::uint32_t>; reference.write_32bit = reference_write<std::uint32_t>;
    reference.read_64bit = reference_read<std::uint64_t>; reference.write_64bit = reference_write<std::uint64_t>;
    reference.load_context(validation_before);
    validation_running = true;
    reference.run(count);
    validation_running = false;
    bool same = true;
    for (unsigned r = 0; r < 16; ++r) {
        auto actual = cpu->Reg[r];
        if (r == 15) actual &= cpu->TFlag ? ~1u : ~3u;
        if (actual != reference.get_reg(r)) {
            fprintf(stderr, "AOT VERIFY pc=%08X count=%u R%u compiled=%08X reference=%08X\n",
                validation_before.cpu_registers[15],count,r,actual,reference.get_reg(r));
            same = false;
        }
    }
    const auto flags = (cpu->NFlag<<31)|(cpu->ZFlag<<30)|(cpu->CFlag<<29)|(cpu->VFlag<<28)|(cpu->TFlag<<5);
    if ((reference.get_cpsr() & 0xF0000020) != flags) {
        fprintf(stderr,"AOT VERIFY flags pc=%08X count=%u compiled=%08X reference=%08X\n",
            validation_before.cpu_registers[15],count,flags,reference.get_cpsr()); same=false;
    }
    for (const auto &[addr, value] : validation_memory) {
        auto it = reference_writes.find(addr);
        const auto expected = it == reference_writes.end() ? value : it->second;
        if (cpu->ReadMemory8(addr) != expected) { fprintf(stderr,"AOT VERIFY memory %08X\n",addr);same=false;break; }
    }
    for (const auto &[addr, value] : reference_writes)
        if (cpu->ReadMemory8(addr) != value) { fprintf(stderr,"AOT VERIFY reference write %08X\n",addr);same=false;break; }
    if (!same) {
        fprintf(stderr,"AOT VERIFY entry mode=%u code:", (validation_before.cpsr>>5)&1);
        for (unsigned i=0;i<32;i+=4) fprintf(stderr," %08X",cpu->ReadCode(validation_before.cpu_registers[15]+i));
        fprintf(stderr,"\n");
        for(unsigned i=0;i<16;++i) fprintf(stderr," r%u=%08X",i,validation_before.cpu_registers[i]);
        fprintf(stderr,"\n");
        std::abort();
    }
    validating = false;
}



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

static void flush_hot_blocks() {
    if (hot_pending.empty()) return;
    auto bytes = build_wasm_module(hot_pending, {{"env","tlb_read32",2,true},{"env","tlb_write32",3,false},
        {"env","tlb_read8",2,true},{"env","tlb_write8",3,false},
        {"env","tlb_read16",2,true},{"env","tlb_write16",3,false}});
    stage_aot_module(std::move(bytes), "hot-rom");
    instantiate_staged_modules();
    hot_pending.clear();
}

void configure_hot_rom(const std::uint8_t *host, std::uint32_t base, std::uint32_t size, bool enabled) {
    hot_rom = host; hot_rom_base = base; hot_rom_size = size;
    hot_compilation_enabled = enabled;
    completed_function_count = 0;
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
}

void invalidate_ram_code(std::uint32_t address, std::size_t size) {
    ram_cache.invalidate(address, size);
}

template<bool Profile, bool PublishGuards>
static aot_func lookup_compiled_impl(ARMul_State *cpu) {
    if constexpr(Profile) if(exit_census::enabled)census_entry=nullptr;
    if (validation_running) return nullptr;
    const auto pc = cpu->Reg[15], pc_mode = pc | cpu->TFlag;
    // Existing ROM functions use immutable bytes and need no mapping lookup.
    if (!ram_compilation_enabled || (pc >= hot_rom_base && pc - hot_rom_base < hot_rom_size)) {
        if constexpr (PublishGuards) cpu->aot_code_begin = cpu->aot_code_end = 0; // ROM is immutable.
        auto function = global_registry().lookup(pc_mode);
        if (Profile && (common::guest_profile::enabled && common::performance::counting()) && !function) common::guest_profile::state.event("rom_missing",pc_mode);
        return function;
    }
    // The normal path avoids the resolver callback on a generation/space hit.
    // Mode 0 also compares compiled bytes; trusted-byte modes omit those scans.
    if (!Profile || !(common::guest_profile::enabled && common::performance::counting())) {
        auto *entry = ram_cache.find(pc_mode, *cpu->parent());
        if (!entry) return nullptr;
        if constexpr(Profile) if(exit_census::enabled)census_entry=entry;
        if constexpr (PublishGuards) {
            cpu->aot_code_begin = static_cast<std::uint32_t>(entry->guard_begin);
            cpu->aot_code_end = static_cast<std::uint32_t>(entry->guard_end);
        }
        return entry->function;
    }
    core::code_mapping view;
    if (!cpu->parent()->resolve_code) return nullptr;
    const bool mapped = cpu->parent()->resolve_code(pc, view);
    auto *entry = ram_cache.find(pc_mode, *cpu->parent());
    if (Profile && (common::guest_profile::enabled && common::performance::counting()) && (!mapped || !entry || !entry->function)) {
        std::uint32_t opcode = 0;
        if (mapped && view.bytes && view.size >= (cpu->TFlag ? 2u : 4u)) std::memcpy(&opcode,view.bytes,cpu->TFlag ? 2 : 4);
        common::guest_profile::state.event(!mapped ? "ram_unmapped" : !entry ? "ram_missing" : entry->rejected ? "ram_rejected" : "ram_pending",pc_mode,view.address_space,opcode);
    }
    if (!mapped || !entry) return nullptr;
    if constexpr(Profile) if(exit_census::enabled)census_entry=entry;
    if constexpr (PublishGuards) {
        cpu->aot_code_begin = static_cast<std::uint32_t>(entry->guard_begin);
        cpu->aot_code_end = static_cast<std::uint32_t>(entry->guard_end);
    }
    return entry->function;
}

template<bool PublishGuards>
static aot_func lookup_compiled_selected(ARMul_State *cpu) {
    return common::guest_profile::enabled && common::performance::enabled && common::performance::detailed
        ? lookup_compiled_impl<true, PublishGuards>(cpu) : lookup_compiled_impl<false, PublishGuards>(cpu);
}

aot_func lookup_compiled(ARMul_State *cpu) {
    // The executable-byte policy is frozen before modules are generated. Modes
    // with emitted interval readers always retain publication, even when the
    // experiment is selected. Mapping/lifetime validation is unchanged.
    if (omit_guard_publication && common::code_tracking::skip_code_write_guards())
        return lookup_compiled_selected<false>(cpu);
    return lookup_compiled_selected<true>(cpu);
}

template<bool Verify, bool Profile, bool PublishGuards>
static compiled_run execute_chain_impl(ARMul_State *cpu, aot_func function) {
    const auto budget = cpu->aot_budget;
    compiled_run result;
    // The owning core and its embedded TLB storage outlive this chain. Entries
    // still change on remaps; only the address of their fixed array is reused.
    auto *tlb = static_cast<dyncom_core *>(cpu->parent())->mem_cache();
    const auto tlb_address = tlb->page_bits == 12 && tlb->folded_index == r12l1::dyncom_folded_tlb
        ? static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(tlb->entries)) : 0;

    while (function && result.instructions < budget && (!runner_region_limit || result.blocks < runner_region_limit)) {
        tlb->sync_write_protection();
        cpu->aot_budget = budget - result.instructions;
        if constexpr (Profile) count_ram_dispatch(cpu);
        if constexpr (Verify) validation_begin(cpu);
        cpu->aot_tlb = Verify && validating ? 0 : tlb_address;
        cpu->aot_exit = 0;
        const auto entry_pc = cpu->Reg[15] | cpu->TFlag;
        if constexpr(Profile) if(exit_census::enabled) {
            exit_census::last_reason=0;exit_census::effects=0;exit_census::last_constraint=0;
            exit_census::last_pc=0;exit_census::last_opcode=0;
            exit_census::last_restriction=0;exit_census::last_rejected_pc=0;exit_census::last_rejected_opcode=0;
            exit_census::guard_hits=0;exit_census::last_guard_host=0;exit_census::last_guard_size=0;
            exit_census::entry_proof_failed=0;exit_census::entry_overlap_count=0;exit_census::entry_other_failure=0;
            exit_census::entry_proof_attempted=0;exit_census::entry_read_spans=0;exit_census::entry_write_spans=0;
        }
        // Deque-backed entries remain stable across inserts/invalidation. Keep
        // this invocation's entry even if a synchronous helper performs lookup.
        const auto *guard_entry = Profile && exit_census::enabled ? census_entry : nullptr;
        if constexpr(Profile) if(exit_census::counting() && guard_entry) {
            ++exit_census::validated_entries;
            exit_census::validated_primary_bytes += guard_entry->code.size();
            exit_census::validated_dependency_spans += guard_entry->dependencies.size();
            for(const auto &dependency:guard_entry->dependencies)
                exit_census::validated_dependency_bytes += dependency.code.size();
            exit_census::protected_interval_bytes += guard_entry->guard_end-guard_entry->guard_begin;
        }
        const auto count = function(cpu);
        if constexpr(Profile) if(exit_census::counting()) {
            exit_census::entry_proof_attempts+=exit_census::entry_proof_attempted;
            exit_census::entry_read_span_checks+=exit_census::entry_read_spans;
            exit_census::entry_write_span_checks+=exit_census::entry_write_spans;
            exit_census::entry_proof_fallbacks += bool(exit_census::entry_proof_failed);
            unsigned gaps=0,snapshots=0;
            for(unsigned n=0;n<std::min(32u,exit_census::entry_overlap_count);++n) {
                const auto &span=exit_census::entry_overlaps[n];
                const char *outcome=!guard_entry?"unavailable_entry":
                    validated_code_cache::diagnostic_code_overlap(*guard_entry,span.host,span.bytes)?"snapshot_overlap":"interval_gap_only";
                ++exit_census::entry_proof_outcomes[outcome];
                if(guard_entry) {if(std::string(outcome)=="interval_gap_only")++gaps;else ++snapshots;}
            }
            if(exit_census::entry_overlap_count>32)exit_census::entry_overlap_dropped+=exit_census::entry_overlap_count-32;
            if(exit_census::entry_proof_failed) {
                const char *cause=exit_census::entry_overlap_count>32 || (!guard_entry && exit_census::entry_overlap_count)?"unclassified_overlap":
                    gaps && !snapshots && !exit_census::entry_other_failure?"interval_gaps_only":
                    gaps?"gap_with_other_failure":snapshots?"snapshot_overlap":"other_guard";
                ++exit_census::entry_fallback_causes[cause];
            }
        }
        if constexpr(Profile) if(exit_census::counting() && (exit_census::effects&2)) {
            const char *outcome=!exit_census::guard_hits?"uncaptured_guard":!guard_entry?"unavailable_entry":exit_census::guard_hits!=1?"multiple_guards":
                validated_code_cache::diagnostic_code_overlap(*guard_entry,exit_census::last_guard_host,exit_census::last_guard_size)
                    ?"snapshot_overlap":"interval_gap_only";
            ++exit_census::code_guard_outcomes[outcome];
        }
        if constexpr(Profile) exit_census::record(entry_pc,cpu->Reg[15]|cpu->TFlag,
            cpu->parent()->code_address_space,count,cpu->aot_budget,cpu->aot_exit);
        if (Profile && common::guest_profile::enabled && common::performance::counting()) {
            auto &profile = common::guest_profile::state;
            ++profile.block_lengths[count];
            if (++profile.compiled_blocks % profile.stride == 0) {
                core::code_mapping view;
                if (cpu->parent()->resolve_code) cpu->parent()->resolve_code(entry_pc & ~1u,view);
                const auto width = (entry_pc & 1) ? 2u : 4u;
                std::uint32_t last = 0;
                if (!region_enabled && count && cpu->parent()->resolve_code && cpu->parent()->resolve_code((entry_pc & ~1u)+(count-1)*width,view)
                    && view.size >= width) std::memcpy(&last,view.bytes,width);
                profile.edge(entry_pc,cpu->Reg[15] | cpu->TFlag,view.address_space,count,last);
            }
        }
        if constexpr (Verify) validation_end(cpu, count);
        if (count > cpu->aot_budget) std::abort(); // generated-code contract
        if (Profile && (common::guest_profile::enabled && common::performance::counting()) && !count) common::guest_profile::state.event("compiled_zero",cpu->Reg[15] | cpu->TFlag);
        ++result.blocks;
        result.instructions += count;
        if (!count || !cpu->NumInstrsToExecute || result.instructions == budget || (!cpu->NirqSig && !(cpu->Cpsr & 0x80))) break;
        cpu->Reg[15] &= cpu->TFlag ? ~1u : ~3u;
        // This stays inside the compiled runner. Every RAM successor retains
        // mapping/lifetime validation. Byte-mutation detection is policy-dependent;
        // trusted-byte modes intentionally permit stale code after guest writes.
        function = lookup_compiled_impl<Profile, PublishGuards>(cpu);
    }
    if constexpr(Profile) if(exit_census::counting()) {
        const char *why = !cpu->NumInstrsToExecute ? "stop" : result.instructions==budget ? "budget"
            : (!cpu->NirqSig && !(cpu->Cpsr&0x80)) ? "interrupt"
            : !function ? "successor_unavailable" : (runner_region_limit && result.blocks==runner_region_limit) ? "region_cap" : "zero_progress";
        ++exit_census::runners[why];
    }
    return result;
}

template<bool PublishGuards>
static compiled_run execute_chain_selected(ARMul_State *cpu, aot_func function) {
    // Diagnostic configuration is fixed before guest threads start. Preserve
    // phase-dependent counting in the diagnostic runner, but omit its branches
    // entirely in normal play and counter-free timing runs.
    if (verification_stride()) return execute_chain_impl<true, true, PublishGuards>(cpu, function);
    if (common::performance::enabled && common::performance::detailed)
        return execute_chain_impl<false, true, PublishGuards>(cpu, function);
    return execute_chain_impl<false, false, PublishGuards>(cpu, function);
}

compiled_run execute_chain(ARMul_State *cpu, aot_func function) {
    // Choose once per outer runner, not once per compiled region or guest store.
    if (omit_guard_publication && common::code_tracking::skip_code_write_guards())
        return execute_chain_selected<false>(cpu, function);
    return execute_chain_selected<true>(cpu, function);
}

std::uint32_t execute_single(ARMul_State *cpu, aot_func function) {
    cpu->mem_cache_->sync_write_protection();
    count_ram_dispatch(cpu);
    if (!verification_stride()) return function(cpu);
    validation_begin(cpu);
    const auto count = function(cpu);
    validation_end(cpu, count);
    return count;
}

void observe_hot_pc(ARMul_State *cpu) {
#ifdef __EMSCRIPTEN__
    if (!hot_compilation_enabled || validation_running || (++hot_dispatches & 31)) return;
    if ((hot_dispatches & 8191) == 0) flush_hot_blocks();
    const auto pc = cpu->Reg[15], key = pc | cpu->TFlag;
    if (pc < hot_rom_base || pc - hot_rom_base >= hot_rom_size) {
        if (!ram_compilation_enabled || !cpu->parent()->resolve_code) return;
        if (ram_cache.versions() >= 16384) {
            if ((common::guest_profile::enabled && common::performance::counting())) common::guest_profile::state.event("ram_capacity",key);
            return;
        }
        core::code_mapping view;
        if (!cpu->parent()->resolve_code(pc, view)) return;
        if (ram_cache.find(key, *cpu->parent())) return; // compiled or awaiting instantiation
        const auto identity = validated_code_cache::key(view.address_space, key);
        if (ram_counts.size() >= 131072 && !ram_counts.count(identity)) return;
        auto &count = ram_counts[identity];
        if (++count % 8) {
            if ((common::guest_profile::enabled && common::performance::counting())) common::guest_profile::state.event("candidate_threshold",key,view.address_space);
            return;
        }
        const auto size = std::min(std::size_t(chaining_enabled ? primary_window_bytes : 256), view.size);
        leaf_resolver leaves = [&](std::uint32_t target) {
            core::code_mapping leaf;
            if (!cpu->parent()->resolve_code(target, leaf) || leaf.address_space != view.address_space)
                return std::vector<std::uint8_t>{};
            const auto bytes = std::min(std::size_t(leaf_instruction_limit*4), leaf.size);
            return std::vector<std::uint8_t>(leaf.bytes, leaf.bytes + bytes);
        };
        auto tr = cpu->TFlag ? translate_thumb_block(view.bytes, size, pc, nullptr, nullptr, true, true, chaining_enabled)
                            : translate_arm_block(view.bytes, size, pc, nullptr, nullptr, true, true, chaining_enabled, region_enabled, &leaves, defer_memory_enabled, ir_policy);
        if (tr.func.body.empty() || !tr.entry_supported) {
            // Cache rejection against these exact bytes; retry only after mutation.
            ram_cache.insert(key, view, std::min(size, std::size_t(cpu->TFlag ? 2 : 4))).rejected = true;
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
            std::size_t(cpu->TFlag ? 2 : 4), size);
        auto &entry = ram_cache.insert(key, view, consumed);
        for (const auto &dependency : tr.dependencies) {
            core::code_mapping leaf;
            if (!cpu->parent()->resolve_code(dependency.address, leaf)
                || leaf.address_space != view.address_space || leaf.size < dependency.bytes.size()
                || (!common::code_tracking::skip_code_scans()
                    && !equal_code_bytes(leaf.bytes, dependency.bytes.data(), dependency.bytes.size()))) {
                entry.live = false;
                return;
            }
            validated_code_cache::add_dependency(entry, dependency.address, leaf.bytes, dependency.bytes);
        }
        tr.func.export_name = "r_" + std::to_string(entry.version) + "_pc_" + std::to_string(pc);
        hot_pending.push_back(std::move(tr.func));
        if (common::performance::counting()) ++common::performance::ram_blocks_compiled;
        if (hot_pending.size() >= 32) flush_hot_blocks();
        return;
    }
    if (hot_compiled >= 4096) return;
    if (hot_counts.size() >= 65536 && !hot_counts.count(key)) return;
    auto &count = hot_counts[key];
    if (count >= 8 || ++count != 8) return; // one attempt per immutable entry
    const auto offset = pc - hot_rom_base;
    const auto size = std::min(chaining_enabled ? primary_window_bytes : 128u, hot_rom_size - offset);
    leaf_resolver leaves = [](std::uint32_t target) {
        return resolve_rom_leaf(hot_rom, hot_rom_base, hot_rom_size, target);
    };
    auto tr = cpu->TFlag ? translate_thumb_block(hot_rom + offset, size, pc, nullptr, nullptr, true, false, chaining_enabled)
                        : translate_arm_block(hot_rom + offset, size, pc, nullptr, nullptr, true, false, chaining_enabled, region_enabled, rom_inline_leaves ? &leaves : nullptr, defer_memory_enabled, ir_policy);
    if (tr.func.body.empty() || !tr.entry_supported) return;
    tr.func.export_name = "f_" + std::to_string(key);
    hot_pending.push_back(std::move(tr.func));
    if (hot_pending.size() >= 32) flush_hot_blocks();
    ++hot_compiled;
#endif
}

#ifdef __EMSCRIPTEN__

// Generated arithmetic keeps NZCVT separate from the packed CPSR. Publish them
// after the emitter's state barrier and before a memory/exception callback reads
// the owning core's CPSR. Direct mapped accesses do not call these trampolines.
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
        validation_access(state, arm_addr, 4);
        return state->ReadMemory32(arm_addr);
    }

    EMSCRIPTEN_KEEPALIVE
    void aot_tlb_write32(ARMul_State *state, std::uint32_t arm_addr, std::uint32_t value) {
        publish_callback_cpsr(state);
        validation_access(state, arm_addr, 4);
        state->WriteMemory32(arm_addr, value);
    }

    EMSCRIPTEN_KEEPALIVE
    std::uint32_t aot_tlb_read8(ARMul_State *state, std::uint32_t arm_addr) {
        publish_callback_cpsr(state);
        validation_access(state, arm_addr, 1);
        return state->ReadMemory8(arm_addr);
    }

    EMSCRIPTEN_KEEPALIVE
    std::uint32_t aot_tlb_read16(ARMul_State *state, std::uint32_t arm_addr) {
        publish_callback_cpsr(state);
        validation_access(state, arm_addr, 2);
        return state->ReadMemory16(arm_addr);
    }

    EMSCRIPTEN_KEEPALIVE
    void aot_tlb_write16(ARMul_State *state, std::uint32_t arm_addr, std::uint32_t value) {
        publish_callback_cpsr(state);
        validation_access(state, arm_addr, 2);
        state->WriteMemory16(arm_addr, value);
    }

    EMSCRIPTEN_KEEPALIVE
    void aot_tlb_write8(ARMul_State *state, std::uint32_t arm_addr, std::uint32_t value) {
        publish_callback_cpsr(state);
        validation_access(state, arm_addr, 1);
        state->WriteMemory8(arm_addr, value);
    }
}

// WASM imports use full i32 parameters; narrow inside C++, never in the caller ABI.
// Uninstrumented internal imports. The checked variants above are selected only
// for verifier runs; ordinary memory accesses contain no validation hook.
static std::uint32_t raw_read32(ARMul_State *s, std::uint32_t a) { publish_callback_cpsr(s); return s->ReadMemory32(a); }
static std::uint32_t raw_read16(ARMul_State *s, std::uint32_t a) { publish_callback_cpsr(s); return s->ReadMemory16(a); }
static std::uint32_t raw_read8(ARMul_State *s, std::uint32_t a) { publish_callback_cpsr(s); return s->ReadMemory8(a); }
static void raw_write32(ARMul_State *s, std::uint32_t a, std::uint32_t v) { publish_callback_cpsr(s); s->WriteMemory32(a, v); }
static void raw_write16(ARMul_State *s, std::uint32_t a, std::uint32_t v) { publish_callback_cpsr(s); s->WriteMemory16(a, v); }
static void raw_write8(ARMul_State *s, std::uint32_t a, std::uint32_t v) { publish_callback_cpsr(s); s->WriteMemory8(a, v); }

template<unsigned Index> static void count_memory() {
    if (common::performance::counting()) ++common::guest_profile::state.memory_calls[Index];
}
static std::uint32_t prof_read32(ARMul_State *s, std::uint32_t a) { count_memory<0>(); return raw_read32(s,a); }
static void prof_write32(ARMul_State *s, std::uint32_t a, std::uint32_t v) { count_memory<1>(); raw_write32(s,a,v); }
static std::uint32_t prof_read8(ARMul_State *s, std::uint32_t a) { count_memory<2>(); return raw_read8(s,a); }
static void prof_write8(ARMul_State *s, std::uint32_t a, std::uint32_t v) { count_memory<3>(); raw_write8(s,a,v); }
static std::uint32_t prof_read16(ARMul_State *s, std::uint32_t a) { count_memory<4>(); return raw_read16(s,a); }
static void prof_write16(ARMul_State *s, std::uint32_t a, std::uint32_t v) { count_memory<5>(); raw_write16(s,a,v); }

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
            tlb_read16: raw(4), tlb_write16: raw(5)
        }};

        var instance = new WebAssembly.Instance(wasmModule, importObj);

        // Collect exports and add them to Emscripten's function table
        var results = [];
        for (var name in instance.exports) {
            if (name === 'memory') continue;
            var func = instance.exports[name];
            if (typeof func !== 'function') continue;

            // Add to Emscripten's indirect function table
            var tableIdx = addFunction(func, 'ii'); // (i32) -> i32
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

    const bool verify = verification_stride() != 0;
    const std::uintptr_t helpers[] = {
        reinterpret_cast<std::uintptr_t>(verify ? aot_tlb_read32 : common::guest_profile::enabled ? prof_read32 : raw_read32),
        reinterpret_cast<std::uintptr_t>(verify ? aot_tlb_write32 : common::guest_profile::enabled ? prof_write32 : raw_write32),
        reinterpret_cast<std::uintptr_t>(verify ? aot_tlb_read8 : common::guest_profile::enabled ? prof_read8 : raw_read8),
        reinterpret_cast<std::uintptr_t>(verify ? aot_tlb_write8 : common::guest_profile::enabled ? prof_write8 : raw_write8),
        reinterpret_cast<std::uintptr_t>(verify ? aot_tlb_read16 : common::guest_profile::enabled ? prof_read16 : raw_read16),
        reinterpret_cast<std::uintptr_t>(verify ? aot_tlb_write16 : common::guest_profile::enabled ? prof_write16 : raw_write16)
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

#else // !__EMSCRIPTEN__

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
