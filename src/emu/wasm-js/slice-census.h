/*
 * Copyright (c) 2026 EKA2L1 Team.
 * This file is part of EKA2L1 project and is licensed under GPLv3 or later.
 */

#pragma once

#include <cpu/arm_interface.h>
#include <kernel/kernel.h>
#include <kernel/codeseg.h>
#include <kernel/process.h>
#include <kernel/thread.h>
#include <loader/rom.h>
#include <system/epoc.h>
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <map>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

namespace eka2l1::cooperative_diagnostics {
// Diagnostic-only slice-entry sampling. The weight belongs to the whole
// following system.loop(), not necessarily to this PC's decoded block.
class slice_census {
    using key = std::tuple<std::uint32_t, std::uint32_t, bool, std::uint32_t, std::uint64_t>;
    struct row {
        std::uint32_t asid, pc, process_uid;
        bool thumb, generation_available;
        std::uint64_t generation, slices = 0, instructions = 0;
        std::string process_name, module_name, source_hex;
        std::uint32_t module_begin = 0, module_bytes = 0, source_asid = 0;
        unsigned module_matches = 0;
        bool mapped = false, rom = false;
    };
    std::map<key, std::size_t> indices_;
    std::vector<row> rows_;
    std::uint64_t slices_ = 0, instructions_ = 0, idle_slices_ = 0, idle_instructions_ = 0;
    std::uint64_t dropped_slices_ = 0, dropped_instructions_ = 0, counter_regressions_ = 0;
    static constexpr std::size_t idle = std::numeric_limits<std::size_t>::max();
    static constexpr std::size_t dropped = idle - 1;
    static constexpr std::size_t max_rows = 8192;
    static std::string quote(const std::string &text) {
        std::ostringstream out; out << '"';
        for (const unsigned char c : text) {
            if (c == '"' || c == '\\') out << '\\' << c;
            else if (c < 32) out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << unsigned(c) << std::dec;
            else out << c;
        }
        out << '"'; return out.str();
    }
public:
    struct token { std::size_t index; std::uint64_t instructions_before; };
    token begin(eka2l1::system &system, std::uint64_t instructions_before) {
        auto *cpu = system.get_cpu();
        auto *kern = system.get_kernel_system();
        auto *thread = kern ? kern->crr_thread() : nullptr;
        if (!cpu || !thread) return {idle, instructions_before};
        auto *process = thread->owning_process();
        const auto asid = cpu->code_address_space, pc = cpu->get_pc();
        const bool thumb = cpu->is_thumb_mode();
        const auto uid = process ? process->get_uid() : 0;
        const auto generation = cpu->code_mapping_generation
            ? cpu->code_mapping_generation->load(std::memory_order_acquire) : 0;
        const key identity{asid, pc, thumb, uid, generation};
        if (const auto found = indices_.find(identity); found != indices_.end())
            return {found->second, instructions_before};
        if (rows_.size() == max_rows) return {dropped, instructions_before};
        row next{asid, pc, uid, thumb, cpu->code_mapping_generation != nullptr, generation};
        next.process_name = process ? process->name() : "<no process>";
        // These queries do not attach/load modules or use guest memory access
        // callbacks. An unresolved/ambiguous region stays explicitly unknown.
        for (const auto &object : kern->get_codeseg_list()) {
            if (!object) continue;
            auto *segment = static_cast<kernel::codeseg *>(object.get());
            const auto begin = segment->get_code_run_addr(process), size = segment->get_code_size();
            if (!begin || !size || pc < begin || std::uint64_t(pc) >= std::uint64_t(begin) + size) continue;
            ++next.module_matches;
            if (next.module_matches == 1) {
                next.module_name = segment->name(); next.module_begin = begin;
                next.module_bytes = size; next.rom = segment->is_rom();
            }
        }
        if (!next.module_matches) {
            const auto *rom = system.get_rom_info();
            if (rom && pc >= rom->header.rom_base
                && std::uint64_t(pc) < std::uint64_t(rom->header.rom_base) + rom->header.rom_size) {
                next.module_name = "<ROM image, no loaded module match>";
                next.module_begin = rom->header.rom_base;
                next.module_bytes = rom->header.rom_size; next.rom = true;
            }
        }
        arm::core::code_mapping mapping{};
        if (cpu->resolve_code && cpu->resolve_code(pc, mapping) && mapping.bytes && mapping.size) {
            next.mapped = true; next.source_asid = mapping.address_space;
            static constexpr char hex[] = "0123456789abcdef";
            for (std::size_t n = 0; n < std::min<std::size_t>(mapping.size, 64); ++n) {
                next.source_hex += hex[mapping.bytes[n] >> 4]; next.source_hex += hex[mapping.bytes[n] & 15];
            }
        }
        const auto index = rows_.size();
        rows_.push_back(std::move(next)); indices_.emplace(identity, index);
        return {index, instructions_before};
    }
    void finish(token entry, std::uint64_t instructions_after) {
        ++slices_;
        if (instructions_after < entry.instructions_before) { ++counter_regressions_; return; }
        const auto weight = instructions_after - entry.instructions_before;
        instructions_ += weight;
        if (entry.index == idle) { ++idle_slices_; idle_instructions_ += weight; return; }
        if (entry.index == dropped) { ++dropped_slices_; dropped_instructions_ += weight; return; }
        auto &sample = rows_.at(entry.index); ++sample.slices; sample.instructions += weight;
    }
    std::string json() const {
        std::ostringstream out;
        out << "{\"schema\":1,\"scope\":\"Diagnostic slice-entry PC weighted by the whole following system.loop instruction delta; not exact block frequency or CPU wall time. No frame readback required.\""
            << ",\"max_rows\":" << max_rows << ",\"slices\":" << slices_ << ",\"instructions\":" << instructions_
            << ",\"idle_slices\":" << idle_slices_ << ",\"idle_instructions\":" << idle_instructions_
            << ",\"dropped_slices\":" << dropped_slices_
            << ",\"dropped_instructions\":" << dropped_instructions_ << ",\"counter_regressions\":" << counter_regressions_
            << ",\"rows\":[";
        bool first = true;
        for (const auto &sample : rows_) {
            if (!first) out << ','; first = false;
            out << "{\"asid\":" << sample.asid << ",\"pc\":" << sample.pc << ",\"thumb\":" << (sample.thumb ? "true" : "false")
                << ",\"process_uid\":" << sample.process_uid << ",\"process_name\":" << quote(sample.process_name)
                << ",\"generation_available\":" << (sample.generation_available ? "true" : "false")
                << ",\"generation\":" << sample.generation << ",\"slices\":" << sample.slices
                << ",\"weighted_instructions\":" << sample.instructions << ",\"module_name\":" << quote(sample.module_name)
                << ",\"module_begin\":" << sample.module_begin << ",\"module_bytes\":" << sample.module_bytes
                << ",\"module_matches\":" << sample.module_matches << ",\"rom\":" << (sample.rom ? "true" : "false")
                << ",\"mapped\":" << (sample.mapped ? "true" : "false") << ",\"source_asid\":" << sample.source_asid
                << ",\"source_hex\":" << quote(sample.source_hex) << '}';
        }
        out << "]}"; return out.str();
    }
};
}
