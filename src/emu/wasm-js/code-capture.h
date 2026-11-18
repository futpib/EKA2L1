/*
 * Copyright (c) 2026 EKA2L1 Team.
 * This file is part of EKA2L1 project and is licensed under GPLv3 or later.
 */

#pragma once

#include <cpu/arm_interface.h>
#include <kernel/kernel.h>
#include <kernel/codeseg.h>
#include <kernel/process.h>
#include <mem/process.h>
#include <mem/page.h>
#include <system/epoc.h>
#include <atomic>
#include <cstdint>
#include <set>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace eka2l1::cooperative_diagnostics {
struct mapped_code_capture {
    std::string name, process_name;
    std::uint32_t uid, process_uid, asid, base, bytes;
    std::uint64_t generation, virtual_us, instructions;
    bool rom;
    std::vector<std::uint8_t> code;
};

inline std::string code_capture_name(std::string value) {
    for (auto &c : value) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    return value;
}

// Explicit paused-boundary diagnostic only. These queries neither run guest
// instructions nor attach modules, invoke guest callbacks, or change mappings.
// Returned bytes own their storage; the caller hashes and persists that copy.
inline std::vector<mapped_code_capture> capture_mapped_code(
        eka2l1::system &system, std::uint32_t process_uid,
        const std::vector<std::string> &module_names,
        std::uint64_t virtual_us, std::uint64_t instructions) {
    auto *kern = system.get_kernel_system();
    if (!kern || module_names.empty() || module_names.size() > 32)
        throw std::runtime_error("Mapped-code capture precondition unavailable");
    kernel::process *process = nullptr;
    for (const auto &object : kern->get_process_list()) {
        if (!object) continue;
        auto *candidate = static_cast<kernel::process *>(object.get());
        if (candidate->get_uid() != process_uid || !candidate->get_thread_count()) continue;
        if (process) throw std::runtime_error("Ambiguous live process UID for mapped-code capture");
        process = candidate;
    }
    if (!process || !process->get_mem_model()) throw std::runtime_error("Requested live process unavailable");
    const auto asid = process->get_mem_model()->address_space_id();
    const auto generation = mem::mapping_generation.load(std::memory_order_acquire);
    std::set<std::string> requested;
    std::vector<mapped_code_capture> result;
    std::uint64_t total = 0;
    for (const auto &name : module_names) {
        const auto wanted = code_capture_name(name);
        if (wanted.empty() || !requested.insert(wanted).second)
            throw std::runtime_error("Empty or duplicate module request");
        kernel::codeseg *found = nullptr;
        std::uint32_t base = 0, size = 0;
        for (const auto &object : kern->get_codeseg_list()) {
            if (!object) continue;
            auto *segment = static_cast<kernel::codeseg *>(object.get());
            if (code_capture_name(segment->name()) != wanted) continue;
            const auto address = segment->get_code_run_addr(process);
            if (!address || !segment->get_code_size()) continue;
            if (found) throw std::runtime_error("Ambiguous module mapping");
            found = segment; base = address; size = segment->get_code_size();
        }
        if (!found) throw std::runtime_error("Requested module mapping unavailable: " + name);
        if (size > 32u * 1024u * 1024u || std::uint64_t(base) + size > (std::uint64_t(1) << 32)
                || (total += size) > 64u * 1024u * 1024u)
            throw std::runtime_error("Mapped-code capture extent exceeds bound");
        mapped_code_capture row{found->name(), process->name(), std::get<2>(found->get_uids()),
            process->get_uid(), static_cast<std::uint32_t>(asid), base, size,
            generation, virtual_us, instructions, found->is_rom(), std::vector<std::uint8_t>(size)};
        // The actual process implementation walks each mapped page and copies
        // only its valid span. A hole fails before any partial row is returned.
        if (!process->read_memory(base, row.code.data(), size))
            throw std::runtime_error("Incomplete mapped-code page extent: " + name);
        if (found->get_code_run_addr(process) != base || found->get_code_size() != size)
            throw std::runtime_error("Module mapping changed during capture");
        result.push_back(std::move(row));
    }
    if (process->get_uid() != process_uid || !process->get_thread_count()
            || !process->get_mem_model() || process->get_mem_model()->address_space_id() != asid
            || mem::mapping_generation.load(std::memory_order_acquire) != generation)
        throw std::runtime_error("Process mapping identity changed during capture");
    return result;
}
}
