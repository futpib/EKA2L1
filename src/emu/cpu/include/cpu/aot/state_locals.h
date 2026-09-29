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
        // A result block carries the instruction count to one final writeback.
        // Helper barriers still flush/reload at their original positions.
        bool shared_return = false;
        std::uint32_t first_local = 0;
        std::map<std::uint32_t, std::uint32_t> locals;
        std::set<std::uint32_t> written;
        struct barrier { std::size_t position; bool reload; };
        std::vector<barrier> barriers;

        bool accepts(std::uint32_t offset) const {
            using S = state_offsets;
            return enabled && ((offset < S::PC && offset % 4 == 0)
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
            if (enabled) barriers.push_back({position, reload});
        }
        static void leb(std::vector<std::uint8_t> &out, std::uint32_t value) {
            do {
                auto byte = static_cast<std::uint8_t>(value & 127);
                value >>= 7;
                out.push_back(byte | (value ? 128 : 0));
            } while (value);
        }
        void transfer(std::vector<std::uint8_t> &out, bool reload) const {
            for (const auto &[offset, slot] : locals) {
                if (!reload && !written.count(offset)) continue;
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
            std::vector<std::uint8_t> body;
            transfer(body, true);
            if (shared_return) { body.push_back(op_block); body.push_back(type_i32); }
            std::size_t previous = 0;
            for (const auto &point : barriers) {
                body.insert(body.end(), function.body.begin() + previous,
                    function.body.begin() + point.position);
                transfer(body, point.reload);
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
