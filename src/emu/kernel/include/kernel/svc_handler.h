#pragma once

#include <cstdint>
#include <type_traits>

namespace eka2l1::hle {
    enum class svc_return_convention { direct, eka1, epoc91 };

    template<svc_return_convention Convention, typename Cpu, typename Dispatch, typename InRom>
    void invoke_svc(Cpu &cpu, std::uint32_t ordinal, Dispatch dispatch, InRom in_rom) {
        if constexpr (Convention == svc_return_convention::epoc91) {
            // 9.1 ROM stubs leave their return address in r12; trampolines do not.
            if (ordinal != 0xff && in_rom(cpu.get_pc())) {
                const auto target = cpu.get_reg(12);
                const auto flags = (cpu.get_cpsr() & ~0x20u) | ((target & 1) << 5);
                cpu.set_pc(target & ~1u);
                cpu.set_cpsr(flags);
            }
        }
        dispatch(ordinal);
        if constexpr (Convention == svc_return_convention::eka1) {
            const auto target = cpu.get_lr();
            const auto flags = (cpu.get_cpsr() & ~0x20u) | ((target & 1) << 5);
            cpu.set_pc(target & ~1u);
            cpu.set_cpsr(flags);
        }
    }
}
