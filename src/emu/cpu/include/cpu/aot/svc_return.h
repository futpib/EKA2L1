#pragma once

#include <cpu/aot/aot_runtime.h>
#include <cpu/aot/memory_experiment.h>
#include <cpu/dyncom/armstate.h>
#include <cstring>

namespace eka2l1::arm::aot {
    // The compiler proves the immutable instruction pair. The syscall may
    // change the context or mappings; consume only the returned state/view.
    inline std::uint32_t complete_svc_return(ARMul_State *cpu,
            const memory_experiment::direct_view *memory, std::uint32_t request,
            std::uint32_t expected_pc, std::uint32_t expected_lr) {
        if (cpu->Reg[15] != expected_pc || cpu->TFlag
                || cpu->Reg[14] != expected_lr || !(expected_lr & 1)) return 0;

        // SVC fallthrough has no IRQ boundary before BX. Its outgoing edge
        // does, including the raw (unaligned) PC on an interrupt or stop.
        cpu->Reg[15] = expected_lr;
        cpu->TFlag = 1;
        if (!cpu->NumInstrsToExecute
                || (!cpu->NirqSig && !(cpu->Cpsr & 0x80))) return 1;
        cpu->Reg[15] &= ~1u;

        // A failed whole-span proof leaves POP entirely to its original path,
        // including partial accesses, endian handling and exception callbacks.
        if (!memory || (cpu->Cpsr & 0x200)) return 1;
        const auto reg = (request >> svc_return_register_shift) & 15;
        const unsigned bytes = reg < 8 ? 8 : 4;
        const auto sp = cpu->Reg[13];
        std::uint32_t host = 0;
        if (sp - memory_experiment::direct_begin
                < (memory->arena_mask & (memory_experiment::direct_size - bytes + 1))) {
            host = sp + memory->bias;
        } else if ((sp & 4095) <= 4096 - bytes) {
            const auto *pages = reinterpret_cast<const memory_experiment::page *>(memory->pages);
            const auto page = pages[sp >> 12].read;
            if (page) host = page + (sp & 4095);
        }
        if (!host) return 1;
        std::uint32_t value;
        if (reg < 8) {
            std::memcpy(&value, reinterpret_cast<const void *>(host), 4);
            cpu->Reg[reg] = value;
            host += 4;
        }
        std::memcpy(&value, reinterpret_cast<const void *>(host), 4);
        cpu->Reg[15] = value;
        cpu->TFlag = value & 1;
        cpu->Reg[13] = sp + bytes;
        return 2;
    }
}
