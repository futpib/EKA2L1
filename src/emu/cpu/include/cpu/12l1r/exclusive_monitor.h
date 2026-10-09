/*
 * Copyright (c) 2018 MerryMage
 * SPDX-License-Identifier: 0BSD
 *
 * Copyright (c) 2021 EKA2L1 project
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

#include <cpu/12l1r/common.h>
#include <cpu/arm_interface.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <vector>

namespace eka2l1::arm::r12l1 {
    class exclusive_monitor : public arm::exclusive_monitor {
    public:
        /**
         * @param processor_count       Maximum number of processors using this global
         *                              exclusive monitor. Each processor must have a
         *                              unique id.
         */
        explicit exclusive_monitor(const std::size_t processor_count);

        ~exclusive_monitor() override {
        }

        std::size_t get_processor_count() const;

        /// Marks a region containing [address, address+size) to be exclusive to
        /// processor processor_id.
        template <typename T, typename Function>
        T read_and_mark(const std::size_t processor_id, const vaddress address, Function op) {
            static_assert(std::is_trivially_copyable_v<T>);
            const vaddress masked_address = address & RESERVATION_GRANULE_MASK;

            lock();
            may_have_reservations_ = true;
            exclusive_addresses_[processor_id] = masked_address;
            const T value = op();
            std::memcpy(exclusive_values_[processor_id].data(), &value, sizeof(T));
            unlock();

            return value;
        }

        /// Checks to see if processor processor_id has exclusive access to the
        /// specified region. If it does, executes the operation then clears
        /// the exclusive state for processors if their exclusive region(s)
        /// contain [address, address+size).
        template <typename T, typename Function>
        bool do_exclusive_operation(const std::size_t processor_id, vaddress address, Function op) {
            static_assert(std::is_trivially_copyable_v<T>);
            if (!check_and_clear(processor_id, address)) {
#if R12L1_ENABLE_FUZZ
                exclusive_operation_results_[processor_id] = false;
#endif

                return false;
            }

            T saved_value;
            std::memcpy(&saved_value, exclusive_values_[processor_id].data(), sizeof(T));
            const auto result = op(saved_value);

#if R12L1_ENABLE_FUZZ
            exclusive_operation_results_[processor_id] = result;
#endif

            unlock();
            return result;
        }

        struct reservation_snapshot {
            vaddress address;
            std::array<std::uint64_t, 2> value;
            bool operator==(const reservation_snapshot &other) const {
                return address == other.address && value == other.value;
            }
        };
        reservation_snapshot snapshot(std::size_t processor_id) {
            lock();
            reservation_snapshot result{exclusive_addresses_.at(processor_id), exclusive_values_.at(processor_id)};
            unlock(); return result;
        }
        void restore(std::size_t processor_id, const reservation_snapshot &value) {
            lock(); may_have_reservations_ = true;
            exclusive_addresses_.at(processor_id) = value.address;
            exclusive_values_.at(processor_id) = value.value; unlock();
        }
        void clear_processor(const std::size_t processor_id);
        void clear_exclusive() override;

        std::uint8_t exclusive_read8(core *cc, address vaddr) override;
        std::uint16_t exclusive_read16(core *cc, address vaddr) override;
        std::uint32_t exclusive_read32(core *cc, address vaddr) override;
        std::uint64_t exclusive_read64(core *cc, address vaddr) override;

        bool exclusive_write8(core *cc, address vaddr, std::uint8_t value) override;
        bool exclusive_write16(core *cc, address vaddr, std::uint16_t value) override;
        bool exclusive_write32(core *cc, address vaddr, std::uint32_t value) override;
        bool exclusive_write64(core *cc, address vaddr, std::uint64_t value) override;

#if R12L1_ENABLE_FUZZ
        std::vector<bool> exclusive_operation_results_;
#endif

    private:
        bool check_and_clear(const std::size_t processor_id, vaddress address);

        void lock();
        void unlock();

        static constexpr vaddress RESERVATION_GRANULE_MASK = 0xFFFF'FFFFUL;
        static constexpr vaddress INVALID_EXCLUSIVE_ADDRESS = 0xDEAD'DEADUL;

        std::atomic<std::uint8_t> lock_state_{0};
        // Protected by the monitor lock. Unlock publishes its conservative
        // value in bit 1; bit 0 denotes the lock itself.
        bool may_have_reservations_ = false;

        using exclusive_value_array = std::array<std::uint64_t, 2>;

        std::vector<vaddress> exclusive_addresses_;
        std::vector<exclusive_value_array> exclusive_values_;
    };
}
