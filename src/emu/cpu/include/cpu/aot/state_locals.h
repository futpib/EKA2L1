#pragma once

#include <cpu/aot/thumb_translator.h>
#include <cpu/aot/state_liveness.h>
#include <map>
#include <set>

namespace eka2l1::arm::aot {
    // Deferred barriers use the final register set, including registers whose
    // first textual use is after a callback or a conditional early return.
    struct state_local_cache {
        bool enabled = false;
        bool runtime_fields = false;
        bool program_counter = false;
        // A result block carries the instruction count to one final writeback.
        // Helper barriers still flush/reload at their original positions.
        bool shared_return = false;
        std::uint32_t first_local = 0;
        std::map<std::uint32_t, std::uint32_t> locals;
        std::set<std::uint32_t> written;
        struct entry_fields {
            std::shared_ptr<entry_fields> parent;
            std::set<std::uint32_t> used;
            bool needs(std::uint32_t field) const {
                if (!used.count(field)) return false;
                for (auto outer = parent; outer; outer = outer->parent)
                    if (outer->used.count(field)) return false;
                return true;
            }
        };
        std::shared_ptr<entry_fields> entry_scope, active_scope;
        struct barrier {
            std::size_t position;
            bool reload;
            std::shared_ptr<entry_fields> scope;
        };
        std::vector<barrier> barriers;
        // Re-establish derived memory locals at entry and after every helper.
        std::vector<std::uint8_t> reload_suffix;

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
            if (active_scope) active_scope->used.insert(offset);
            auto it = locals.find(offset);
            if (it != locals.end()) return it->second;
            const auto slot = first_local + static_cast<std::uint32_t>(locals.size());
            locals.emplace(offset, slot);
            return slot;
        }
        void barrier_at(std::size_t position, bool reload = false) {
            if (enabled) barriers.push_back({position, reload, {}});
        }
        static void leb(std::vector<std::uint8_t> &out, std::uint32_t value) {
            do {
                auto byte = static_cast<std::uint8_t>(value & 127);
                value >>= 7;
                out.push_back(byte | (value ? 128 : 0));
            } while (value);
        }
        void transfer(std::vector<std::uint8_t> &out, bool reload,
                std::vector<state_transfer> *transfers = nullptr,
                const entry_fields *scope = nullptr) const {
            for (const auto &[offset, slot] : locals) {
                if (scope && !scope->needs(offset)) continue;
                if (!reload && !written.count(offset)) continue;
                const auto begin = out.size();
                out.push_back(op_local_get); leb(out, 0);
                if (reload) {
                    out.push_back(op_i32_load); leb(out, 2); leb(out, offset);
                    out.push_back(op_local_set); leb(out, slot);
                } else {
                    out.push_back(op_local_get); leb(out, slot);
                    out.push_back(op_i32_store); leb(out, 2); leb(out, offset);
                }
                if (transfers) transfers->push_back({begin, out.size(), slot, reload});
            }
            if (reload && (!scope || !scope->parent))
                out.insert(out.end(), reload_suffix.begin(), reload_suffix.end());
        }
        void finish(wasm_func_def &function, bool prune = true) {
            if (!enabled) return;

            std::vector<std::uint8_t> body;
            source_marks sources;
            std::vector<state_transfer> transfers;
            std::vector<std::uint8_t> reload, flush;
            std::vector<state_transfer> reload_transfers, flush_transfers;
            transfer(reload, true, &reload_transfers);
            transfer(flush, false, &flush_transfers);
            struct scoped_transfer { std::vector<std::uint8_t> code; std::vector<state_transfer> tags; };
            std::map<const entry_fields *, scoped_transfer> scoped;
            const auto prepare = [&](const std::shared_ptr<entry_fields> &scope) {
                if (scope && !scoped.count(scope.get())) {
                    auto &value = scoped[scope.get()];
                    transfer(value.code, true, &value.tags, scope.get());
                }
            };
            prepare(entry_scope);
            for (const auto &point : barriers) prepare(point.scope);
            const auto &initial = entry_scope ? scoped[entry_scope.get()].code : reload;
            auto bytes = function.body.size() + initial.size();
            auto records = reload_transfers.size();
            for (const auto &point : barriers) {
                bytes += point.scope ? scoped[point.scope.get()].code.size()
                    : point.reload ? reload.size() : flush.size();
                records += point.reload ? reload_transfers.size() : flush_transfers.size();
            }
            if (shared_return) { bytes += flush.size() + 4; records += flush_transfers.size(); }
            body.reserve(bytes); transfers.reserve(records);
            // Encode each barrier kind once, then copy it with relocated tags.
            const auto append_transfer = [&](bool load, const entry_fields *scope = nullptr) {
                const auto &code = scope ? scoped[scope].code : load ? reload : flush;
                const auto &tags = scope ? scoped[scope].tags : load ? reload_transfers : flush_transfers;
                const auto start = body.size();
                if (!function.sources.empty()) {
                    sources.push_back({static_cast<std::uint32_t>(start), 0, source_kind::state});
                    if (load && (!scope || !scope->parent) && !reload_suffix.empty())
                        sources.push_back({static_cast<std::uint32_t>(start + code.size() - reload_suffix.size()), 0, source_kind::memory_check});
                }
                body.insert(body.end(), code.begin(), code.end());
                for (auto tag : tags) {
                    tag.begin += start; tag.end += start;
                    transfers.push_back(tag);
                }
            };
            append_transfer(true, entry_scope.get());
            if (shared_return) { body.push_back(op_block); body.push_back(type_i32); }
            // Segment call operands were recorded before deferred barriers.
            // Relocate them using the final cache layout, before inserting bytes.
            for (auto &call : function.outlined_calls) {
                const auto original = call.call_offset;
                call.call_offset += static_cast<std::uint32_t>(body.size());
                for (const auto &point : barriers) if (point.position <= original)
                    call.call_offset += static_cast<std::uint32_t>(point.scope ? scoped[point.scope.get()].code.size()
                        : point.reload ? reload.size() : flush.size());
            }
            std::size_t previous = 0;
            for (const auto &point : barriers) {
                copy_source_marks(function.sources, previous, point.position, body.size(), sources);
                body.insert(body.end(), function.body.begin() + previous,
                    function.body.begin() + point.position);
                append_transfer(point.reload, point.scope.get());
                previous = point.position;
            }
            copy_source_marks(function.sources, previous, function.body.size(), body.size(), sources);
            body.insert(body.end(), function.body.begin() + previous, function.body.end());
            if (shared_return) {
                body.push_back(op_end);
                append_transfer(false);
                body.push_back(op_return);
            }
            function.body = std::move(body);
            function.sources = std::move(sources);
            function.num_locals += static_cast<std::uint32_t>(locals.size());
            if (prune) prune_state_transfers(function, transfers, first_local,
                static_cast<std::uint32_t>(locals.size()));
        }
    };
}
