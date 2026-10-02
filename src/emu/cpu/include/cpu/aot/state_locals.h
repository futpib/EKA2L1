#pragma once

#include <cpu/aot/thumb_translator.h>
#include <map>
#include <set>

namespace eka2l1::arm::aot {
    // Deferred barriers use the final register set, including registers whose
    // first textual use is after a callback or a conditional early return.
    struct state_local_cache {
        bool enabled = false;
        bool runtime_fields = false;
        bool program_counter = false;
        // Only for acyclic emission: publish writes preceding this barrier.
        // Reloads still use the final locals, including future helper operands.
        bool prefix_writeback = false;
        // A result block carries the instruction count to one final writeback.
        // Helper barriers still flush/reload at their original positions.
        bool shared_return = false;
        std::uint32_t first_local = 0;
        std::map<std::uint32_t, std::uint32_t> locals;
        std::set<std::uint32_t> written;
        struct barrier { std::size_t position; bool reload; std::uint64_t writes; };
        std::vector<barrier> barriers;

        bool accepts(std::uint32_t offset) const {
            using S = state_offsets;
            return enabled && ((offset < S::PC && offset % 4 == 0)
                || (program_counter && offset == S::PC)
                || offset == S::NFLAG || offset == S::ZFLAG || offset == S::CFLAG
                || offset == S::VFLAG || offset == S::TFLAG
                || (runtime_fields && (offset == S::CPSR
                    || (offset >= S::AOT_BUDGET && offset <= S::AOT_EXIT))));
        }
        std::uint32_t local(std::uint32_t offset) {
            auto it = locals.find(offset);
            if (it != locals.end()) return it->second;
            const auto slot = first_local + static_cast<std::uint32_t>(locals.size());
            locals.emplace(offset, slot);
            return slot;
        }
        void barrier_at(std::size_t position, bool reload = false) {
            if (!enabled) return;
            std::uint64_t mask = ~std::uint64_t{0};
            if (prefix_writeback && !reload) {
                mask = 0;
                for (const auto offset : written) {
                    const auto index = locals.at(offset) - first_local;
                    if (index < 64) mask |= std::uint64_t{1} << index;
                }
            }
            barriers.push_back({position, reload, mask});
        }
        static void leb(std::vector<std::uint8_t> &out, std::uint32_t value) {
            do {
                auto byte = static_cast<std::uint8_t>(value & 127);
                value >>= 7;
                out.push_back(byte | (value ? 128 : 0));
            } while (value);
        }
        void transfer(std::vector<std::uint8_t> &out, bool reload, std::uint64_t mask = ~std::uint64_t{0}) const {
            for (const auto &[offset, slot] : locals) {
                if (!reload && !written.count(offset)) continue;
                if (!reload && prefix_writeback && !(mask & (std::uint64_t{1} << (slot - first_local)))) continue;
                out.push_back(op_local_get); leb(out, 0);
                if (reload) {
                    out.push_back(op_i32_load); leb(out, 2); leb(out, offset);
                    out.push_back(op_local_set); leb(out, slot);
                } else {
                    out.push_back(op_local_get); leb(out, slot);
                    out.push_back(op_i32_store); leb(out, 2); leb(out, offset);
                }
            }
        }
        void finish(wasm_func_def &function) {
            if (!enabled) return;
            // Current architectural caches have fewer than 32 fields. Future
            // expansion beyond the mask retains conservative writeback.
            if (locals.size() > 64) prefix_writeback = false;
            std::vector<std::uint8_t> body;
            transfer(body, true);
            if (shared_return) { body.push_back(op_block); body.push_back(type_i32); }
            // Segment call operands were recorded before deferred barriers.
            // Relocate them using the final cache layout, before inserting bytes.
            std::vector<std::uint8_t> flush;
            transfer(flush, false);
            const auto reload_size = body.size() - (shared_return ? 2u : 0u);
            for (auto &call : function.outlined_calls) {
                const auto original = call.call_offset;
                call.call_offset += static_cast<std::uint32_t>(body.size());
                for (const auto &point : barriers) if (point.position <= original) {
                    if (prefix_writeback && !point.reload) {
                        std::vector<std::uint8_t> selected;
                        transfer(selected, false, point.writes);
                        call.call_offset += static_cast<std::uint32_t>(selected.size());
                    } else call.call_offset += static_cast<std::uint32_t>(point.reload ? reload_size : flush.size());
                }
            }
            std::size_t previous = 0;
            for (const auto &point : barriers) {
                body.insert(body.end(), function.body.begin() + previous,
                    function.body.begin() + point.position);
                transfer(body, point.reload, point.writes);
                previous = point.position;
            }
            body.insert(body.end(), function.body.begin() + previous, function.body.end());
            if (shared_return) {
                body.push_back(op_end);
                transfer(body, false);
                body.push_back(op_return);
            }
            function.body = std::move(body);
            function.num_locals += static_cast<std::uint32_t>(locals.size());
        }
    };
}
