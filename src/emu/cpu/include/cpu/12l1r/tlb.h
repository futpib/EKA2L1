/*
 * Copyright (c) 2020 EKA2L1 Team.
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

#pragma once

#include <common/types.h>
#include <common/code_tracking.h>
#include <cpu/12l1r/common.h>

#include <algorithm>
#include <cstdint>
#include <cstring>

namespace eka2l1::arm::r12l1 {
    struct tlb_entry {
        vaddress read_addr;
        vaddress write_addr;
        vaddress execute_addr;

        std::uint8_t *host_base;

        explicit tlb_entry()
            : read_addr(0)
            , write_addr(0)
            , execute_addr(0)
            , host_base(nullptr) {
        }
    };

    static constexpr std::uint32_t TLB_LOOKUP_BIT_COUNT = 9;
    static constexpr std::uint32_t TLB_ENTRY_COUNT = 1 << TLB_LOOKUP_BIT_COUNT;
    static constexpr std::uint32_t TLB_ENTRY_MASK = TLB_ENTRY_COUNT - 1;

    // Research configuration, frozen before DynCom cores/regions are created.
    // Native 12l1r cores retain their fixed low-bit index contract.
    inline bool dyncom_folded_tlb = false;

    struct tlb {
    public:
        tlb_entry entries[TLB_ENTRY_COUNT];

        std::size_t page_bits;
        std::size_t page_mask;
        const bool folded_index;
#if defined(__EMSCRIPTEN__) && defined(EKA2L1_WASM_CODE_WRITE_PROTECTION)
        std::uint64_t protected_generation = 0;
#endif

        explicit tlb(std::size_t page_bits, bool folded = false)
            : page_bits(page_bits), folded_index(folded) {
            page_mask = (1 << page_bits) - 1;
            flush();
        }

        std::size_t index(vaddress addr) const {
            const auto page = addr >> page_bits;
            return (folded_index ? page ^ (page >> TLB_LOOKUP_BIT_COUNT) : page) & TLB_ENTRY_MASK;
        }

        // Called before every generated function, after lookup can start
        // watching new code. Only this CPU's own TLB is changed. Generated
        // per-function page/address proofs never survive that call boundary.
        void sync_write_protection() {
#if defined(__EMSCRIPTEN__) && defined(EKA2L1_WASM_CODE_WRITE_PROTECTION)
            namespace tracking = eka2l1::common::code_tracking;
            if (!tracking::protect_writes) return;
            const auto generation = tracking::watch_generation;
            if (generation && protected_generation == generation) return;
            for (auto &entry : entries)
                if (entry.write_addr && tracking::write_needs_callback(entry.host_base,page_mask+1))
                    entry.write_addr = 0;
            protected_generation = generation;
#endif
        }

        void flush() {
            // Using memfill to speed up this process
            std::memset(entries, 0, sizeof(tlb_entry) * TLB_ENTRY_COUNT);
        }

        void add(vaddress addr, std::uint8_t *host, const std::uint32_t perm) {
            const std::size_t tlb_index = index(addr);
            const std::size_t addr_mod = addr & page_mask;
            const vaddress addr_normed = addr & ~page_mask;

            tlb_entry &entry = entries[tlb_index];
            entry.host_base = host - addr_mod;

            if (perm & prot_read) {
                entry.read_addr = addr_normed;
            } else {
                entry.read_addr = 0;
            }

            bool allow_write = (perm & prot_write) != 0;
#if defined(__EMSCRIPTEN__) && defined(EKA2L1_WASM_CODE_WRITE_PROTECTION)
            // A refilled alias must not recover direct write access to watched
            // physical backing. The MMU callback retains the real permission.
            allow_write = allow_write && !eka2l1::common::code_tracking::write_needs_callback(entry.host_base,page_mask+1);
#endif
            if (allow_write) {
                entry.write_addr = addr_normed;
            } else {
                entry.write_addr = 0;
            }

            if (perm & prot_exec) {
                entry.execute_addr = addr_normed;
            } else {
                entry.execute_addr = 0;
            }
        }

        void make_dirty(const vaddress addr) {
            const std::size_t tlb_index = index(addr);
            const vaddress addr_normed = addr & ~page_mask;

            tlb_entry &entry = entries[tlb_index];

            if ((entry.read_addr == addr_normed) || (entry.write_addr == addr_normed) || (entry.execute_addr == addr_normed)) {
                std::memset(&entry, 0, sizeof(tlb_entry));
            }
        }

        // Permission-specific lookup. Zero tags mean absent, so page zero must
        // use callbacks rather than accidentally matching an absent permission.
        template<std::uint32_t Permission>
        std::uint8_t *lookup_access(const vaddress addr) {
            static_assert(Permission == prot_read || Permission == prot_write || Permission == prot_exec);
            const vaddress page = addr & ~page_mask;
            const auto &entry = entries[index(addr)];
            const vaddress tag = Permission == prot_read ? entry.read_addr
                : Permission == prot_write ? entry.write_addr : entry.execute_addr;
            return page && entry.host_base && tag == page
                ? entry.host_base + (addr & page_mask) : nullptr;
        }

        std::uint8_t *lookup(const vaddress addr) {
            const std::size_t tlb_index = index(addr);
            const vaddress addr_normed = addr & ~page_mask;

            tlb_entry &entry = entries[tlb_index];

            if (!entry.host_base) {
                return nullptr;
            }

            if ((entry.read_addr == addr_normed) || (entry.write_addr == addr_normed) || (entry.execute_addr == addr_normed)) {
                const std::size_t addr_mod = addr & page_mask;
                return entry.host_base + addr_mod;
            }

            // TLB miss
            return nullptr;
        }
    };
}