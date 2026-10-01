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

#include <common/code_tracking.h>
#include <cpu/aot/arm_translator.h>
#include <cpu/aot/state_locals.h>
#include <cpu/aot/exit_census.h>
#include <cpu/aot/execution_limits.h>
#include <cpu/aot/region_ir.h>
#include <cpu/12l1r/tlb.h>

#include <cstring>
#include <algorithm>
#include <map>
#include <set>

namespace eka2l1::arm::aot {
    using S = state_offsets;

    static void leb(std::vector<std::uint8_t> &out, std::uint32_t v) {
        do {
            std::uint8_t b = v & 0x7F;
            v >>= 7;
            if (v) b |= 0x80;
            out.push_back(b);
        } while (v);
    }

    static void sleb(std::vector<std::uint8_t> &out, std::int32_t v) {
        bool more = true;
        while (more) {
            std::uint8_t b = v & 0x7F;
            v >>= 7;
            if ((v == 0 && !(b & 0x40)) || (v == -1 && (b & 0x40)))
                more = false;
            else
                b |= 0x80;
            out.push_back(b);
        }
    }

    struct arm_emit {
        std::vector<std::uint8_t> &b;
        state_local_cache cache;
        bool region = false;
        bool defer_memory = false, restartable_access = false;
        std::uint32_t current_pc = 0;
        bool pc_written = false;
        // Reserved i32 locals for region instruction count and memory fast path.
        static constexpr unsigned COUNT=9, ADDRESS=10, VALUE=11, HOST=12, ENTRY=13, READ_PAGE=14, READ_BASE=15, WRITE_PAGE=16, WRITE_BASE=17;
        bool memory_write = false;
        bool instruction_may_exit = true;
        bool entry_supported = true;
        bool unsupported = false;
        std::uint32_t bail_count = 0;
        // Offset within an entry-proved straight-line chunk. Cold exits consume
        // it without changing the representation on the successful path.
        unsigned count_offset = 0;
        void count_value() {
            get_local(COUNT);
            if (count_offset) { i32_const(count_offset); op(op_i32_add); }
        }
        void commit_count() {
            if (!count_offset) return;
            count_value(); set_local(COUNT); count_offset = 0;
        }
        // A straight-line long-multiply value can represent two guest registers.
        // Exits reconstruct their exact halves; control/helper boundaries end
        // the representation. Local 1 is the reserved i64 multiply result.
        int wide_lo = -1, wide_hi = -1;
        // Entry-proved host spans, indexed by the lexical memory instruction.
        struct proved_access { unsigned host; std::uint32_t offset; };
        std::map<std::uint32_t, proved_access> proved_accesses;
        bool has_proved_access() const { return proved_accesses.count(current_pc); }
        void proved_host() {
            const auto &access = proved_accesses.at(current_pc);
            get_local(access.host);
            if (access.offset) { i32_const(access.offset); op(op_i32_add); }
            set_local(HOST);
        }

        void materialize_wide() {
            if (wide_lo < 0) return;
            get_local(1); op(op_i32_wrap_i64);
            store_i32_from_stack(S::reg(wide_lo), 2);
            get_local(1); op(op_i64_const); b.push_back(32); op(op_i64_shr_u);
            op(op_i32_wrap_i64); store_i32_from_stack(S::reg(wide_hi), 2);
        }
        void end_wide() {
            materialize_wide(); wide_lo = wide_hi = -1;
        }

        // Depth inside the result block added by state_local_cache::finish.
        unsigned scope_depth = 0;
        void op(std::uint8_t o) {
            if (o == op_block || o == op_loop || o == op_if) ++scope_depth;
            else if (o == op_end) --scope_depth;
            b.push_back(o);
        }
        void state_ptr() { op(op_local_get); leb(b, 0); }
        void get_local(std::uint32_t i) { op(op_local_get); leb(b, i); }
        void set_local(std::uint32_t i) { op(op_local_set); leb(b, i); }
        void tee_local(std::uint32_t i) { op(op_local_tee); leb(b, i); }
        void i32_const(std::int32_t v) { op(op_i32_const); sleb(b, v); }

        void load_i32(std::uint32_t offset) {
            if (cache.accepts(offset)) { get_local(cache.local(offset)); return; }
            state_ptr();
            op(op_i32_load); leb(b, 2); leb(b, offset);
        }
        void store_i32(std::uint32_t offset, std::uint32_t local) {
            if (cache.accepts(offset)) {
                get_local(local); set_local(cache.local(offset)); cache.written.insert(offset); return;
            }
            state_ptr();
            get_local(local);
            op(op_i32_store); leb(b, 2); leb(b, offset);
        }
        void store_i32_from_stack(std::uint32_t offset, std::uint32_t scratch) {
            if (cache.accepts(offset)) {
                set_local(cache.local(offset)); cache.written.insert(offset); return;
            }
            set_local(scratch);
            state_ptr(); get_local(scratch);
            op(op_i32_store); leb(b, 2); leb(b, offset);
        }
        void store_i32_const(std::uint32_t offset, std::int32_t val) {
            if (cache.accepts(offset)) {
                i32_const(val); set_local(cache.local(offset)); cache.written.insert(offset); return;
            }
            state_ptr();
            i32_const(val);
            op(op_i32_store); leb(b, 2); leb(b, offset);
        }

        void load_reg(int r) {
            if (r == wide_lo || r == wide_hi) {
                get_local(1);
                if (r == wide_hi) { op(op_i64_const); b.push_back(32); op(op_i64_shr_u); }
                op(op_i32_wrap_i64);
            } else if (region && r == 15 && !pc_written) i32_const(current_pc);
            else load_i32(S::reg(r));
        }
        void store_reg(int r, std::uint32_t local) {
            if (r == 15) pc_written = true;
            store_i32(S::reg(r), local);
        }

        void slow_call(std::uint32_t func_idx) {
            instruction_may_exit = true;
            if (region && !pc_written) store_i32_const(S::PC,current_pc);
            cache.barrier_at(b.size());
            op(op_call); leb(b, func_idx);
            cache.barrier_at(b.size(), true);
            if (region) {
                store_i32_const(S::AOT_EXIT, 1); census_effect(1);
                i32_const(-1); set_local(READ_PAGE);
                i32_const(-1); set_local(WRITE_PAGE);
            }
        }
        void tlb_index(unsigned address_local) {
            get_local(address_local); i32_const(12); op(op_i32_shr_u);
            if (r12l1::dyncom_folded_tlb) {
                get_local(address_local); i32_const(12 + r12l1::TLB_LOOKUP_BIT_COUNT);
                op(op_i32_shr_u); op(op_i32_xor);
            }
            i32_const(r12l1::TLB_ENTRY_MASK); op(op_i32_and); i32_const(4); op(op_i32_shl);
        }
        void call(std::uint32_t func_idx) {
            instruction_may_exit = true;
            const bool write = func_idx == 1 || func_idx == 3 || func_idx == 5;
            if (write) memory_write = true;
            if (!region || func_idx > 5) { slow_call(func_idx); return; }
            const unsigned size = func_idx < 2 ? 4 : func_idx < 4 ? 1 : 2;
            if (write) set_local(VALUE);
            set_local(ADDRESS);
            set_local(HOST); // consume state_ptr; HOST is overwritten below
            if (has_proved_access()) {
                instruction_may_exit = false;
                // The entry pass accepted only word accesses and proved both
                // their permissions and physical non-alias with current code.
                proved_host(); get_local(HOST);
                if (write) get_local(VALUE);
                op(write ? op_i32_store : op_i32_load); leb(b, 2); leb(b, 0);
                if (write) track_write();
                return;
            }
            const auto page_local = write ? WRITE_PAGE : READ_PAGE;
            const auto base_local = write ? WRITE_BASE : READ_BASE;
            // Invalid keys are -1, which no aligned page/alignment mask can
            // produce. A key match therefore proves base, permission and endian
            // validity without a second base test or a final HOST branch.
            op(op_block); op(write ? type_void : type_i32);
            get_local(ADDRESS); i32_const(-4096 | (size-1)); op(op_i32_and);
            get_local(page_local); op(op_i32_ne);
            op(op_if); op(type_void);
            i32_const(0); set_local(HOST);
            load_i32(S::AOT_TLB); tee_local(ENTRY);
            op(op_if); op(type_void);
            tlb_index(ADDRESS);
            get_local(ENTRY); op(op_i32_add); set_local(ENTRY);
            get_local(ENTRY); op(op_i32_load); leb(b,2); leb(b,write ? 4 : 0);
            get_local(ADDRESS); i32_const(-4096); op(op_i32_and); op(op_i32_eq);
            get_local(ADDRESS); i32_const(4096); op(op_i32_ge_u); op(op_i32_and);
            get_local(ADDRESS); i32_const(size-1); op(op_i32_and); op(op_i32_eqz); op(op_i32_and);
            load_i32(S::CPSR); i32_const(0x200); op(op_i32_and); op(op_i32_eqz); op(op_i32_and);
            op(op_if); op(type_void);
            get_local(ENTRY); op(op_i32_load); leb(b,2); leb(b,12); set_local(HOST);
            op(op_end); op(op_end);
            get_local(HOST); op(op_i32_eqz); op(op_if); op(type_void);
            if (defer_memory && restartable_access) {
                get_local(COUNT); i32_const(1); op(op_i32_sub); set_local(COUNT);
                bail(current_pc, 0, exit_census::memory);
            } else {
                state_ptr(); get_local(ADDRESS); if(write) get_local(VALUE);
                slow_call(func_idx);
                op(op_br); leb(b,2); // leave result block, preserving helper result
            }
            op(op_end);
            get_local(HOST); set_local(base_local);
            get_local(ADDRESS); i32_const(-4096); op(op_i32_and); set_local(page_local);
            op(op_end); // key miss
            get_local(base_local); get_local(ADDRESS); i32_const(4095); op(op_i32_and);
            op(op_i32_add); set_local(HOST);
            get_local(HOST);
            if (write) get_local(VALUE);
            op(write ? (size==4 ? op_i32_store : size==2 ? op_i32_store16 : op_i32_store8)
                     : (size==4 ? op_i32_load : size==2 ? op_i32_load16_u : op_i32_load8_u));
            leb(b, size==4 ? 2 : size==2 ? 1 : 0); leb(b,0);
            if (write) {
                track_write();
                // Backing-address guard catches writes through guest aliases.
                get_local(HOST); load_i32(S::AOT_CODE_END); op(op_i32_lt_u);
                get_local(HOST); i32_const(size); op(op_i32_add); load_i32(S::AOT_CODE_BEGIN); op(op_i32_gt_u);
                op(op_i32_and); op(op_if); op(type_void);
                store_i32_const(S::AOT_EXIT,1); census_effect(2); census_code_guard(size); op(op_end);
            }
            op(op_end); // result block
        }
        // Validate a whole block-transfer span once. It must be aligned and
        // contained in one permitted TLB page; otherwise use the existing
        // per-access path, including its fault/endian behavior.
        void block_transfer_host(unsigned address_local, unsigned bytes, bool write, unsigned alignment = 4) {
            i32_const(0); set_local(HOST);
            load_i32(S::AOT_TLB); tee_local(ENTRY);
            op(op_if); op(type_void);
            get_local(address_local); i32_const(alignment - 1); op(op_i32_and); op(op_i32_eqz);
            get_local(address_local); i32_const(4095); op(op_i32_and);
            i32_const(4096 - bytes); op(op_i32_le_u); op(op_i32_and);
            load_i32(S::CPSR); i32_const(0x200); op(op_i32_and); op(op_i32_eqz); op(op_i32_and);
            op(op_if); op(type_void);
            tlb_index(address_local);
            get_local(ENTRY); op(op_i32_add); set_local(ENTRY);
            get_local(ENTRY); op(op_i32_load); leb(b,2); leb(b,write ? 4 : 0);
            get_local(address_local); i32_const(-4096); op(op_i32_and); op(op_i32_eq);
            // Zero is the no-permission TLB tag, not an authorized page-zero mapping.
            get_local(address_local); i32_const(4096); op(op_i32_ge_u); op(op_i32_and);
            op(op_if); op(type_void);
            get_local(ENTRY); op(op_i32_load); leb(b,2); leb(b,12); set_local(HOST);
            get_local(HOST); op(op_if); op(type_void);
            get_local(HOST); get_local(address_local); i32_const(4095); op(op_i32_and); op(op_i32_add); set_local(HOST);
            op(op_end); op(op_end); op(op_end); op(op_end);
        }
        // IR guards share the ordinary emitter's page proofs. Helpers invalidate
        // these local keys; successful graph memory effects cannot change a TLB
        // mapping or endian mode. Read and write permission proofs stay separate.
        void ir_memory_host(unsigned bytes, bool write) {
            const unsigned page = write ? WRITE_PAGE : READ_PAGE;
            const unsigned base = write ? WRITE_BASE : READ_BASE;
            get_local(ADDRESS); i32_const(-4096 | (std::min(bytes, 4u) - 1)); op(op_i32_and);
            get_local(page); op(op_i32_eq);
            if (bytes > 4) {
                get_local(ADDRESS); i32_const(4095); op(op_i32_and);
                i32_const(4096 - bytes); op(op_i32_le_u); op(op_i32_and);
            }
            op(op_if); op(type_void);
            get_local(base); get_local(ADDRESS); i32_const(4095); op(op_i32_and);
            op(op_i32_add); set_local(HOST);
            op(op_else);
            block_transfer_host(ADDRESS, bytes, write, std::min(bytes, 4u));
            get_local(HOST); op(op_if); op(type_void);
            get_local(HOST); get_local(ADDRESS); i32_const(4095); op(op_i32_and);
            op(op_i32_sub); set_local(base);
            get_local(ADDRESS); i32_const(-4096); op(op_i32_and); set_local(page);
            op(op_end); op(op_end);
        }
        // All direct stores are aligned and confined to one physical page.
        // Helpers/interpreter writes use the same backing-indexed versions.
        void track_write() {
#if defined(__EMSCRIPTEN__) && defined(EKA2L1_WASM_CODE_VERSIONS)
#if defined(EKA2L1_WASM_CODE_WRITE_PROTECTION)
            if (common::code_tracking::protect_writes) return;
#endif
            get_local(HOST); i32_const(12); op(op_i32_shr_u);
            i32_const(3); op(op_i32_shl);
            i32_const(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(common::code_tracking::pages)));
            op(op_i32_add); set_local(ENTRY);
            get_local(ENTRY); op(op_i32_load); leb(b,2); leb(b,0); set_local(VALUE);
            get_local(VALUE); op(op_if); op(type_void);
            get_local(ENTRY); get_local(VALUE); i32_const(1); op(op_i32_add);
            op(op_i32_store); leb(b,2); leb(b,0);
#if defined(EKA2L1_WASM_CODE_LIFECYCLE)
            // Page-version overflow must not rearm an exhausted page when a
            // new region starts watching it. Host flags use atomic operations.
            get_local(VALUE); i32_const(-1); op(op_i32_eq); op(op_if); op(type_void);
            get_local(ENTRY); i32_const(3);
            op(0xfe); leb(b,0x17); leb(b,2); leb(b,4); // i32.atomic.store flags
            op(op_end);
            i32_const(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&common::code_tracking::dirty)));
            i32_const(1); op(0xfe); leb(b,0x17); leb(b,2); leb(b,0);
#endif
            op(op_end);
#endif
        }
        std::uint32_t census_pc=0,census_opcode=0,census_constraint=0;
        std::uint32_t census_restriction=0,census_rejected_pc=0,census_rejected_opcode=0;
        void census_code_guard(unsigned bytes) {
            if(!exit_census::enabled)return;
            i32_const(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&exit_census::last_guard_host)));
            get_local(HOST);op(op_i32_store);leb(b,2);leb(b,0);
            census_store(&exit_census::last_guard_size,bytes);
            i32_const(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&exit_census::guard_hits)));
            i32_const(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&exit_census::guard_hits)));
            op(op_i32_load);leb(b,2);leb(b,0);i32_const(1);op(op_i32_add);
            op(op_i32_store);leb(b,2);leb(b,0);
        }
        void census_store(std::uint32_t *where,std::uint32_t value) {
            if(!exit_census::enabled)return;
            i32_const(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(where)));
            i32_const(value);op(op_i32_store);leb(b,2);leb(b,0);
        }
        void census_effect(unsigned flag) {
            if(!exit_census::enabled)return;
            i32_const(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&exit_census::effects)));
            i32_const(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&exit_census::effects)));
            op(op_i32_load);leb(b,2);leb(b,0);i32_const(flag);op(op_i32_or);
            op(op_i32_store);leb(b,2);leb(b,0);
        }
        void census_exit(unsigned reason) {
            census_store(&exit_census::last_reason,reason);
            census_store(&exit_census::last_pc,census_pc);
            census_store(&exit_census::last_opcode,census_opcode);
            census_store(&exit_census::last_constraint,census_constraint);
            census_store(&exit_census::last_restriction,census_restriction);
            census_store(&exit_census::last_rejected_pc,census_rejected_pc);
            census_store(&exit_census::last_rejected_opcode,census_rejected_opcode);
        }
        void ret(unsigned why=exit_census::control) {
            census_exit(why);
            // Emitting an exit snapshot must not forget the representation on
            // the other path of the guard being emitted.
            materialize_wide();
            // The return count is already on the stack. Branching carries it
            // to the shared exit and discards any enclosing temporary stack.
            if (cache.shared_return) { op(op_br); leb(b, scope_depth); }
            else { cache.barrier_at(b.size()); op(op_return); }
        }

        void bail(std::uint32_t pc, std::uint32_t instr_count, unsigned why=exit_census::control) {
            store_i32_const(S::PC, static_cast<std::int32_t>(pc));
            if (region) count_value(); else i32_const(static_cast<std::int32_t>(instr_count));
            ret(why);
            bail_count++;
        }

        void bail_preserve_pc(std::uint32_t instr_count) {
            if (region) count_value(); else i32_const(static_cast<std::int32_t>(instr_count));
            ret();
            bail_count++;
        }

        void bail_unsupported(std::uint32_t pc, std::uint32_t instr_count) {
            if (region) { get_local(COUNT); i32_const(1); op(op_i32_sub); set_local(COUNT); }
            unsupported = true;
            if (!instr_count) entry_supported = false;
            bail(pc, instr_count, exit_census::unsupported);
        }
    };

    // Shared lowering for a proved whole region or a pure integer segment.
    // Entry state is read before parallel snapshot destinations are assigned.
    static void emit_ir_values(arm_emit &w, const region_ir &ir,
        const std::vector<bool> &live, const std::vector<unsigned> &locals,
        bool publish_pc, const std::vector<bool> &cold = {},
        std::vector<std::pair<std::size_t, unsigned>> *wide_fixups = nullptr, bool memoize_cold = false,
        const std::vector<bool> &stack_values = {}) {
        auto local = [&](unsigned opcode, unsigned v) {
            if (wide_fixups && ir.nodes[v].type == type_i64) {
                w.op(static_cast<std::uint8_t>(opcode));
                wide_fixups->emplace_back(w.b.size(), locals[v]);
                // Fixed width keeps barrier offsets stable until suffix slots
                // are known, after all architectural locals have been allocated.
                w.b.insert(w.b.end(), {0x80, 0x80, 0x80, 0x80, 0});
            } else if (opcode == op_local_get) w.get_local(locals[v]);
            else w.set_local(locals[v]);
        };
        // Share pure expression lowering between hot values and exit recipes.
        // Memory operations never enter this path: their results can precede
        // stores that overwrite the original source location.
        auto emit_pure = [&](auto &&input, const region_ir::node &node) {
            input(node.a);
            switch (node.op) {
            case region_ir::choose:
                input(node.b); input(static_cast<unsigned>(node.immediate)); w.op(op_select); break;
            case region_ir::pack:
                w.op(op_i64_extend_i32_u); input(node.b); w.op(op_i64_extend_i32_u);
                w.op(op_i64_const); w.b.push_back(32); w.op(op_i64_shl); w.op(op_i64_or); break;
            case region_ir::low: w.op(op_i32_wrap_i64); break;
            case region_ir::high:
                w.op(op_i64_const); w.b.push_back(32); w.op(op_i64_shr_u); w.op(op_i32_wrap_i64); break;
            default:
                if (node.b) input(node.b);
                w.op(static_cast<std::uint8_t>(node.op)); break;
            }
        };
        std::vector<bool> cold_ready(ir.nodes.size());
        auto push_value = [&](auto &&self, unsigned v) -> void {
            const auto &node = ir.nodes[v];
            if (node.op == region_ir::constant) {
                if (node.type == type_i32) w.i32_const(static_cast<std::int32_t>(node.immediate));
                else {
                    w.op(op_i64_const);
                    auto n = static_cast<std::int64_t>(node.immediate);
                    bool more = true;
                    while (more) {
                        auto byte = static_cast<std::uint8_t>(n & 127); n >>= 7;
                        more = !((n == 0 && !(byte & 64)) || (n == -1 && (byte & 64)));
                        w.b.push_back(byte | (more ? 128 : 0));
                    }
                }
            } else if (!stack_values.empty() && stack_values[v]) {
                auto input = [&](unsigned operand) { self(self, operand); };
                emit_pure(input, node);
            } else if (!cold.empty() && cold[v]) {
                auto input = [&](unsigned operand) { self(self, operand); };
                if (!memoize_cold) emit_pure(input, node);
                else {
                    // A shared DAG must not expand exponentially, e.g. repeated
                    // squaring. These slots are assigned inside this exit only.
                    if (!cold_ready[v]) {
                        emit_pure(input, node); local(op_local_set, v); cold_ready[v] = true;
                    }
                    local(op_local_get, v);
                }
            } else if (node.op == region_ir::host) w.get_local(static_cast<unsigned>(node.immediate));
            else if (node.op == region_ir::state) w.load_i32(static_cast<unsigned>(node.immediate));
            else local(op_local_get, v);
        };
        auto push = [&](unsigned v) { push_value(push_value, v); };
        auto snapshot = [&](unsigned index, bool pc) {
            // Separate failure arms cannot rely on another arm's assignments.
            std::fill(cold_ready.begin(), cold_ready.end(), false);
            const auto &entry = ir.snapshots.front(); const auto &exit = ir.snapshots.at(index);
            // Consume every source before changing any architectural local.
            std::vector<unsigned> destinations;
            for (unsigned r = 0; r < 16; ++r)
                if ((r == 15 && pc) || (r != 15 && entry.regs[r] != exit.regs[r])) {
                    push(exit.regs[r]); destinations.push_back(S::reg(r));
                }
            for (unsigned f = 0; f < 5; ++f) if (entry.flags[f] != exit.flags[f]) {
                push(exit.flags[f]); destinations.push_back(region_ir::flag_offsets[f]);
            }
            for (auto it = destinations.rbegin(); it != destinations.rend(); ++it)
                w.store_i32_from_stack(*it, 2);
        };
        for (unsigned v = 1; v < ir.nodes.size(); ++v) {
            if (!live[v] || (!cold.empty() && cold[v])
                || (!stack_values.empty() && stack_values[v])) continue;
            const auto &node = ir.nodes[v];
            if (node.op == region_ir::constant || node.op == region_ir::state || node.op == region_ir::host) continue;
            if (!region_ir::is_effect(node.op)) {
                emit_pure(push, node);
                if (node.type != type_void) local(op_local_set, v);
                continue;
            }
            push(node.a);
            switch (node.op) {
            case region_ir::guarded_host: {
                const unsigned bytes = static_cast<unsigned>(ir.nodes[node.b].immediate);
                const bool write = node.immediate & 1;
                const unsigned exit = static_cast<unsigned>(node.immediate / 2);
                w.set_local(arm_emit::ADDRESS);
                w.ir_memory_host(bytes, write);
                w.get_local(arm_emit::HOST); w.op(op_i32_eqz);
                if (write) {
                    // A store to translated code must return before effects,
                    // including writes through a different guest alias.
                    w.get_local(arm_emit::HOST); w.load_i32(S::AOT_CODE_END); w.op(op_i32_lt_u);
                    w.get_local(arm_emit::HOST); w.i32_const(bytes); w.op(op_i32_add);
                    w.load_i32(S::AOT_CODE_BEGIN); w.op(op_i32_gt_u); w.op(op_i32_and); w.op(op_i32_or);
                    w.get_local(arm_emit::HOST); w.i32_const(bytes); w.op(op_i32_add);
                    w.get_local(arm_emit::HOST); w.op(op_i32_lt_u); w.op(op_i32_or);
                }
                w.op(op_if); w.op(type_void);
                snapshot(exit, true);
                // The enclosing segment charged its first instruction before
                // entering the graph. A failed access itself is not completed.
                w.get_local(arm_emit::COUNT); w.i32_const(static_cast<int>(ir.snapshots[exit].count) - 1);
                w.op(op_i32_add); w.set_local(arm_emit::COUNT);
                w.bail_preserve_pc(0);
                w.op(op_end);
                w.get_local(arm_emit::HOST);
                break;
            }
            case region_ir::read32: case region_ir::read8u: case region_ir::read8s:
            case region_ir::read16u: case region_ir::read16s: {
                const bool word = node.op == region_ir::read32;
                const bool half = node.op == region_ir::read16u || node.op == region_ir::read16s;
                w.op(word ? op_i32_load : node.op == region_ir::read8s ? op_i32_load8_s
                    : node.op == region_ir::read16s ? op_i32_load16_s : half ? op_i32_load16_u : op_i32_load8_u);
                leb(w.b, word ? 2 : half ? 1 : 0); leb(w.b, static_cast<unsigned>(node.immediate)); break;
            }
            case region_ir::write32: case region_ir::write8: case region_ir::write16:
                push(node.b);
                w.op(node.op == region_ir::write32 ? op_i32_store : node.op == region_ir::write16 ? op_i32_store16 : op_i32_store8);
                leb(w.b, node.op == region_ir::write32 ? 2 : node.op == region_ir::write16 ? 1 : 0);
                leb(w.b, static_cast<unsigned>(node.immediate)); break;
            }
            if (node.type != type_void) local(op_local_set, v);
        }
        snapshot(static_cast<unsigned>(ir.snapshots.size() - 1), publish_pc);
    }

    // Emit condition check. ARM condition code in bits [31:28].
    // Emits: if (cond) { ... } with the caller responsible for closing the block.
    // Returns true if a conditional block was opened (caller must arm_emit op_end).
    // Returns false for AL/NV (unconditional).
    static bool emit_cond_check(arm_emit &w, std::uint32_t cond,
                                std::uint32_t TMP1, std::uint32_t TMP2) {
        // AL (14) and NV (15) are unconditional
        if (cond >= 0xE) return false;

        // Evaluate condition based on flags
        switch (cond) {
        case 0: // EQ: Z==1
            w.load_i32(S::ZFLAG);
            break;
        case 1: // NE: Z==0
            w.load_i32(S::ZFLAG);
            w.op(op_i32_eqz);
            break;
        case 2: // CS/HS: C==1
            w.load_i32(S::CFLAG);
            break;
        case 3: // CC/LO: C==0
            w.load_i32(S::CFLAG);
            w.op(op_i32_eqz);
            break;
        case 4: // MI: N==1
            w.load_i32(S::NFLAG);
            break;
        case 5: // PL: N==0
            w.load_i32(S::NFLAG);
            w.op(op_i32_eqz);
            break;
        case 6: // VS: V==1
            w.load_i32(S::VFLAG);
            break;
        case 7: // VC: V==0
            w.load_i32(S::VFLAG);
            w.op(op_i32_eqz);
            break;
        case 8: // HI: C==1 && Z==0
            w.load_i32(S::CFLAG);
            w.load_i32(S::ZFLAG);
            w.op(op_i32_eqz);
            w.op(op_i32_and);
            break;
        case 9: // LS: C==0 || Z==1
            w.load_i32(S::CFLAG);
            w.op(op_i32_eqz);
            w.load_i32(S::ZFLAG);
            w.op(op_i32_or);
            break;
        case 10: // GE: N==V
            w.load_i32(S::NFLAG);
            w.load_i32(S::VFLAG);
            w.op(op_i32_eq);
            break;
        case 11: // LT: N!=V
            w.load_i32(S::NFLAG);
            w.load_i32(S::VFLAG);
            w.op(op_i32_ne);
            break;
        case 12: // GT: Z==0 && N==V
            w.load_i32(S::ZFLAG);
            w.op(op_i32_eqz);
            w.load_i32(S::NFLAG);
            w.load_i32(S::VFLAG);
            w.op(op_i32_eq);
            w.op(op_i32_and);
            break;
        case 13: // LE: Z==1 || N!=V
            w.load_i32(S::ZFLAG);
            w.load_i32(S::NFLAG);
            w.load_i32(S::VFLAG);
            w.op(op_i32_ne);
            w.op(op_i32_or);
            break;
        default:
            return false;
        }
        w.op(op_if); w.op(type_void);
        return true;
    }

    // Decode ARM shifter operand (operand 2, bits [11:0]).
    // I=1: 8-bit immediate rotated right by 2*rotate_imm
    // I=0: register Rm shifted by shift_type
    //
    // Emits WASM code that leaves the operand value on the stack.
    // If carry_out is needed (for S-bit logical ops), sets TMP_CARRY.
    static void emit_shifter_operand(arm_emit &w, std::uint32_t inst,
                                     std::uint32_t TMP1, std::uint32_t TMP2,
                                     std::uint32_t TMP_CARRY, std::uint32_t pc) {
        bool I = (inst >> 25) & 1;
        if (I) {
            // Immediate: val = imm8 ROR (rotate_imm * 2)
            std::uint32_t imm8 = inst & 0xFF;
            std::uint32_t rotate_imm = (inst >> 8) & 0xF;
            std::uint32_t rot = rotate_imm * 2;
            std::uint32_t val;
            if (rot == 0) {
                val = imm8;
            } else {
                val = (imm8 >> rot) | (imm8 << (32 - rot));
            }
            w.i32_const(static_cast<std::int32_t>(val));
            // Carry out for rotated immediate
            if (rot > 0) {
                w.i32_const(static_cast<std::int32_t>((val >> 31) & 1));
                w.set_local(TMP_CARRY);
            }
        } else {
            // Register with shift
            int rm = inst & 0xF;
            std::uint32_t shift_type = (inst >> 5) & 3;
            bool reg_shift = (inst >> 4) & 1;

            // Get Rm value. For Rm==15, value is PC+8 in ARM mode.
            if (rm == 15) {
                w.i32_const(pc + (reg_shift ? 12 : 8));
            } else {
                w.load_reg(rm);
            }
            w.set_local(TMP1);

            if (!reg_shift) {
                // Immediate shift amount
                std::uint32_t shift_imm = (inst >> 7) & 0x1F;
                if (shift_imm == 0 && shift_type == 0) {
                    // No shift: just Rm
                    w.get_local(TMP1);
                } else {
                    unsigned bit = shift_type == 0 ? 32 - shift_imm
                        : shift_imm ? shift_imm - 1 : shift_type == 3 ? 0 : 31;
                    w.get_local(TMP1); w.i32_const(bit); w.op(op_i32_shr_u);
                    w.i32_const(1); w.op(op_i32_and); w.set_local(TMP_CARRY);
                    switch (shift_type) {
                    case 0: // LSL
                        w.get_local(TMP1);
                        w.i32_const(static_cast<std::int32_t>(shift_imm));
                        w.op(op_i32_shl);
                        break;
                    case 1: // LSR
                        if (shift_imm == 0) shift_imm = 32;
                        if (shift_imm == 32) {
                            w.i32_const(0);
                        } else {
                            w.get_local(TMP1);
                            w.i32_const(static_cast<std::int32_t>(shift_imm));
                            w.op(op_i32_shr_u);
                        }
                        break;
                    case 2: // ASR
                        if (shift_imm == 0) shift_imm = 32;
                        if (shift_imm >= 32) {
                            w.get_local(TMP1);
                            w.i32_const(31);
                            w.op(op_i32_shr_s);
                        } else {
                            w.get_local(TMP1);
                            w.i32_const(static_cast<std::int32_t>(shift_imm));
                            w.op(op_i32_shr_s);
                        }
                        break;
                    case 3: // ROR / RRX
                        if (shift_imm == 0) {
                            // RRX: (C << 31) | (Rm >> 1)
                            w.load_i32(S::CFLAG);
                            w.i32_const(31);
                            w.op(op_i32_shl);
                            w.get_local(TMP1);
                            w.i32_const(1);
                            w.op(op_i32_shr_u);
                            w.op(op_i32_or);
                        } else {
                            w.get_local(TMP1);
                            w.i32_const(static_cast<std::int32_t>(shift_imm));
                            w.op(op_i32_rotr);
                        }
                        break;
                    }
                }
            } else {
                // Register shifts use the low byte; WASM's modulo-32 shift
                // semantics need explicit ARM cases for zero and >=32 amounts.
                int rs = (inst >> 8) & 0xF;
                w.load_reg(rs); w.i32_const(255); w.op(op_i32_and); w.set_local(TMP2);
                w.get_local(TMP2); w.op(op_if); w.op(type_void);
                if (shift_type == 3) {
                    w.get_local(TMP1); w.get_local(TMP2); w.op(op_i32_rotr); w.set_local(TMP1);
                    w.get_local(TMP1); w.i32_const(31); w.op(op_i32_shr_u); w.set_local(TMP_CARRY);
                } else {
                    w.get_local(TMP2); w.i32_const(32); w.op(op_i32_lt_u);
                    w.op(op_if); w.op(type_void);
                    w.get_local(TMP1);
                    if (shift_type == 0) { w.i32_const(32); w.get_local(TMP2); w.op(op_i32_sub); }
                    else { w.get_local(TMP2); w.i32_const(1); w.op(op_i32_sub); }
                    w.op(op_i32_shr_u); w.i32_const(1); w.op(op_i32_and); w.set_local(TMP_CARRY);
                    w.get_local(TMP1); w.get_local(TMP2);
                    w.op(shift_type == 0 ? op_i32_shl : shift_type == 1 ? op_i32_shr_u : op_i32_shr_s); w.set_local(TMP1);
                    w.op(op_else);
                    if (shift_type == 2) {
                        w.get_local(TMP1); w.i32_const(31); w.op(op_i32_shr_u); w.set_local(TMP_CARRY);
                        w.get_local(TMP1); w.i32_const(31); w.op(op_i32_shr_s); w.set_local(TMP1);
                    } else {
                        w.get_local(TMP2); w.i32_const(32); w.op(op_i32_eq);
                        w.op(op_if); w.op(type_i32);
                        w.get_local(TMP1); w.i32_const(shift_type == 0 ? 0 : 31); w.op(op_i32_shr_u); w.i32_const(1); w.op(op_i32_and);
                        w.op(op_else); w.i32_const(0); w.op(op_end); w.set_local(TMP_CARRY);
                        w.i32_const(0); w.set_local(TMP1);
                    }
                    w.op(op_end);
                }
                w.op(op_end);
                w.get_local(TMP1);
            }
        }
    }

    // Emit N/Z flag update from result in local `res`.
    static void emit_nz_flags(arm_emit &w, std::uint32_t res, std::uint32_t tmp) {
        w.get_local(res);
        w.i32_const(31);
        w.op(op_i32_shr_u);
        w.store_i32_from_stack(S::NFLAG, tmp);
        w.get_local(res);
        w.op(op_i32_eqz);
        w.store_i32_from_stack(S::ZFLAG, tmp);
    }

    // Only short straight-line leaves with an unchanged LR can be inlined.
    // Memory instructions retain the normal region guards and helper exits.
    static std::vector<std::uint8_t> resolve_leaf(const leaf_resolver &resolve, std::uint32_t address, const char *&failure, bool allow_predicates, exit_census::leaf_refusal &refusal) {
        failure="callee_unsupported";
        auto bytes = resolve(address);
        exit_census::probe(address,bytes.data(),bytes.size());
        if(bytes.empty()) {failure="callee_unmapped_or_other_space";return {};}
        for (std::size_t n = 0; n < leaf_instruction_limit*4 && n + 4 <= bytes.size(); n += 4) {
            std::uint32_t op; std::memcpy(&op, bytes.data() + n, 4);
            if (op == 0xe12fff1e) { bytes.resize(n + 4); return bytes; }
            const auto group = (op >> 26) & 3;
            // Conditional integer operations already have precise lowering in
            // the original emitter. They do not alter the linear control path;
            // false predicates still consume an instruction. Keep conditional
            // memory, transfers and reserved encodings outside this experiment.
            const auto reject = [&](unsigned detail) {
                if(exit_census::enabled){refusal.detail=detail;refusal.pc=address+static_cast<std::uint32_t>(n);refusal.opcode=op;}
                return std::vector<std::uint8_t>{};
            };
            using namespace exit_census;
            if ((op >> 28) != 14 && (!allow_predicates || (op >> 28) == 15 || group != 0))
                return reject(!allow_predicates?predicates_disabled:(op>>28)==15?reserved_predicate:group==1?conditional_memory:conditional_transfer);
            if (group > 1) return reject(group==3?coprocessor_or_supervisor:
                ((op>>25)&7)==5?(op&(1u<<24)?nested_call:internal_branch):block_transfer);
            if (((op >> 16) & 15) >= 13) return reject(sp_operand+((op>>16)&15)-13);
            if (((op >> 12) & 15) >= 13) return reject(sp_operand+((op>>12)&15)-13);
            if (group == 1) {
                if ((op & (1u << 25)) && (op & 15) >= 13) return reject(sp_index+(op&15)-13);
                if ((op & (1u << 25)) && (op & 16)) return reject(register_memory_shift);
            } else {
                // Exclude status transfers, misc instructions, and halfword forms.
                const auto alu = (op >> 21) & 15;
                if (alu >= 8 && alu <= 11 && !(op & (1u << 20))) return reject(status_or_misc);
                if (!(op & (1u << 25))) {
                    if ((op & 15) >= 13) return reject(sp_index+(op&15)-13);
                    if ((op & 16) && ((op >> 8) & 15) >= 13) return reject(sp_shift+((op>>8)&15)-13);
                    if ((op & 0x90) == 0x90) return reject((op&0x0f0000f0u)==0x00000090u?multiply:
                        (op&0x60)?halfword_or_signed_transfer:(op&0x01000000)?swap_or_exclusive:other_extra_transfer);
                }
            }
        }
        failure=bytes.size()>=leaf_instruction_limit*4?"leaf_instruction_limit":"callee_mapping_extent";
        return {};
    }

    // CFG walker for ARM code. Returns reachable 4-byte-aligned offsets.
    static std::set<std::size_t> find_reachable_offsets_arm(
        const std::uint8_t *code, std::size_t code_size, bool bounded,
        std::uint32_t start_address, const leaf_resolver *leaves,
        std::map<std::size_t, code_dependency> &inlined, std::map<std::size_t,exit_census::leaf_refusal> &refusals, bool allow_predicates)
    {
        std::set<std::size_t> reachable;
        if (code_size < 4) return reachable;
        std::vector<std::size_t> worklist;
        worklist.push_back(0);
        while (!worklist.empty()) {
            std::size_t i = worklist.back();
            worklist.pop_back();
            while (i + 3 < code_size) {
                if (reachable.count(i)) break;
                reachable.insert(i);
                std::uint32_t inst;
                std::memcpy(&inst, code + i, 4);
                std::uint32_t cond = (inst >> 28) & 0xF;

                // B/BL: bits [27:25] = 101
                if (((inst >> 25) & 7) == 5) {
                    std::int32_t offset = inst & 0x00FFFFFF;
                    if (offset & 0x00800000) offset |= 0xFF000000; // sign extend
                    offset = (offset << 2) + 8; // PC+8 pipeline
                    std::int32_t target_off = static_cast<std::int32_t>(i) + offset;
                    bool is_link = (inst >> 24) & 1;
                    if (is_link) {
                        if(exit_census::enabled)refusals[i].constraint=cond!=14?7:!leaves?6:inlined.size()>=inline_site_limit?1:0;
                        // Bounded BL exits to the runner. Its return address is
                        // a separate entry, not reachable fallthrough in this region.
                        if (bounded && cond >= 0xE) {
                            if (cond == 14 && leaves && inlined.size() < inline_site_limit) {
                                const auto address = start_address + static_cast<std::uint32_t>(target_off);
                                const char *failure=nullptr;
                                exit_census::leaf_refusal refusal;
                                auto bytes = resolve_leaf(*leaves, address, failure, allow_predicates, refusal);
                                if(exit_census::enabled)refusals[i]=refusal;
                                exit_census::compile_site(start_address+static_cast<std::uint32_t>(i),inst,bytes.empty()?failure:"call_inlined");
                                if(exit_census::enabled && bytes.empty())refusals[i].constraint=
                                    std::strcmp(failure,"leaf_instruction_limit")==0?2:
                                    std::strcmp(failure,"callee_unsupported")==0?3:
                                    std::strcmp(failure,"callee_mapping_extent")==0?4:5;
                                if (!bytes.empty()) {
                                    inlined.emplace(i, code_dependency{address, std::move(bytes)});
                                    i += 4;
                                    continue;
                                }
                            }
                            if(cond==14 && leaves && inlined.size()>=inline_site_limit)
                                exit_census::compile_site(start_address+static_cast<std::uint32_t>(i),inst,"inline_site_limit");
                            break;
                        }
                        // Conditional BL can fall through when its condition fails.
                        i += 4;
                        continue;
                    }
                    if (cond == 0xE || cond == 0xF) {
                        // Unconditional B: follow target, terminate this path
                        if (target_off >= 0 && static_cast<std::size_t>(target_off) + 3 < code_size) {
                            i = static_cast<std::size_t>(target_off);
                            continue;
                        }
                        break; // out of slice
                    }
                    // Conditional B: follow both paths
                    if (target_off >= 0 && static_cast<std::size_t>(target_off) + 3 < code_size) {
                        worklist.push_back(static_cast<std::size_t>(target_off));
                    }
                    i += 4;
                    continue;
                }

                // BX LR / BX Rm: bits [27:4] = 0x12FFF1x
                if ((inst & 0x0FFFFFF0) == 0x012FFF10) {
                    if (cond >= 0xE) {
                        break; // unconditional return
                    }
                    // Conditional: fall through
                    i += 4;
                    continue;
                }

                // LDM/STM with PC in register list
                if (((inst >> 25) & 7) == 4) {
                    bool load = (inst >> 20) & 1;
                    bool has_pc = (inst >> 15) & 1;
                    if (load && has_pc && cond >= 0xE) {
                        break; // LDM {..., PC} — return
                    }
                }

                // MOV PC, ... or data processing with Rd==PC
                if (((inst >> 26) & 3) == 0) {
                    int rd = (inst >> 12) & 0xF;
                    const unsigned opcode = (inst >> 21) & 15;
                    if (rd == 15 && cond >= 0xE && (opcode < 8 || opcode > 11)) {
                        // Tests/MSR do not write Rd; their encoding must not cut off fallthrough.
                        // Could be a return (MOV PC, LR) — terminate
                        break;
                    }
                }

                i += 4;
            }
        }
        return reachable;
    }

    static translate_result translate_arm_block_impl(
        const std::uint8_t *code,
        std::size_t code_size,
        std::uint32_t start_address,
        const sibling_map *siblings,
        const code_window *dll_code, bool bounded, bool stop_after_store, bool cache_registers, bool region, const leaf_resolver *leaves, bool defer_memory, bool allow_memory_proof, arm_ir_policy ir_policy = arm_ir_policy::configured)
    {
        // Policies 14/15 preserve policy 13 semantics and separately vary
        // the segment bound or adjacent single-use pure-value lowering.
        const bool long_ir_segments = ir_policy == arm_ir_policy::long_segments_ir;
        const bool stack_ir_values = ir_policy == arm_ir_policy::stack_values_ir;
        const bool budget_gaps_ir = ir_policy == arm_ir_policy::budget_gaps_ir;
        if (long_ir_segments || stack_ir_values || budget_gaps_ir) ir_policy = arm_ir_policy::conditional_value_ir;
        region = region && bounded;
        // Bounded blocks exit on branches instead of recursively calling siblings.
        // Keep guest-visible instructions (including veneers) in the execution stream.
        if (bounded) { siblings = nullptr; dll_code = nullptr; }
        translate_result tr;
        tr.complete = false;
        wasm_func_def &result = tr.func;
        result.export_name = "f_" + std::to_string(start_address);

        // A fixed i64 prefix keeps its index independent of lazily allocated i32 register locals.
        // Locals: 0=state_ptr(param), 1=wide result, 2..8=i32 scratch.
        result.num_locals = region ? 16 : 7;
        result.num_prefix_i64_locals = 1;
        result.num_f32_locals = 0;
        result.num_f64_locals = 0;
        const std::uint32_t WIDE = 1, TMP1 = 2, TMP2 = 3, TMP3 = 4, TMP4 = 5;
        const std::uint32_t PC_IDX = 6, ADDR_TMP = 7;
        // Separate carry local survives N/Z scratch updates.
        const std::uint32_t TMP_CARRY = 8;

        arm_emit w{result.body};
        w.region = region && bounded;
        w.defer_memory = w.region && defer_memory;
        w.cache.enabled = bounded && cache_registers;
        w.cache.runtime_fields = region;
        w.cache.shared_return = w.cache.enabled;
        w.cache.first_local = result.num_prefix_i64_locals + result.num_locals + 1;

        // Build instruction address → index map
        std::uint32_t num_insns = 0;
        for (std::size_t i = 0; i + 3 < code_size; i += 4) {
            num_insns++;
        }

        std::map<std::size_t, code_dependency> inlined;
        std::map<std::size_t,exit_census::leaf_refusal> refusals;
        auto reachable = find_reachable_offsets_arm(code, code_size, bounded,
            start_address, region ? leaves : nullptr, inlined, refusals, predicated_leaves && ir_policy == arm_ir_policy::write_budget_chunks);
        struct instruction { std::size_t offset; std::uint32_t address, opcode; bool leaf; };
        std::vector<instruction> instructions;
        for (auto i : reachable) {
            std::uint32_t op; std::memcpy(&op, code + i, 4);
            instructions.push_back({i, start_address + static_cast<std::uint32_t>(i), op, false});
            auto it = inlined.find(i);
            if (it == inlined.end()) continue;
            const auto &leaf = it->second;
            if (std::none_of(tr.dependencies.begin(), tr.dependencies.end(), [&](const auto &d) { return d.address == leaf.address; }))
                tr.dependencies.push_back(leaf);
            for (std::size_t n = 0; n < leaf.bytes.size(); n += 4) {
                std::memcpy(&op, leaf.bytes.data() + n, 4);
                instructions.push_back({i, leaf.address + static_cast<std::uint32_t>(n), op, true});
            }
        }

        // A conservative affine pass can prove every ordinary memory span at
        // entry. No successful instruction in this version can call a helper,
        // remap memory, or write current compiled code. Failed proofs exit with
        // zero guest effects into the original compiled function. In particular,
        // writeback/fault ordering must not change to interpreter-block behavior.
        struct affine { int root = -1; std::int64_t offset = 0; };
        struct proof_group { unsigned root; bool write; std::int64_t low, high; unsigned host; };
        struct proof_access { std::uint32_t pc; unsigned group; std::int64_t offset; };
        std::vector<proof_group> proof_groups;
        std::vector<proof_access> proof_accesses;
        bool prove_memory = allow_memory_proof && ir_policy != arm_ir_policy::disabled
            && ir_policy != arm_ir_policy::invariant_reads && ir_policy != arm_ir_policy::invariant_writes && ir_policy != arm_ir_policy::budget_chunks && ir_policy != arm_ir_policy::write_budget_chunks && ir_policy != arm_ir_policy::deferred_chunk_counts && ir_policy != arm_ir_policy::invariant_read_ir && ir_policy != arm_ir_policy::invariant_read_flag_ir && ir_policy != arm_ir_policy::inline_call_ir && ir_policy != arm_ir_policy::invariant_write_ir && ir_policy != arm_ir_policy::conditional_value_ir && w.region && w.defer_memory && cache_registers && !instructions.empty();
#if !defined(EKA2L1_WASM_REGION_IR) || defined(EKA2L1_WASM_CODE_VERSIONS)
        // The guarded IR is an opt-in research path. Version tracking also
        // keeps its existing compiler until separately validated.
        prove_memory = false;
#endif
        affine values[16];
        for (int r = 0; r < 15; ++r) values[r] = {r, 0};
        auto add_access = [&](std::uint32_t pc, affine address, unsigned bytes, bool write) {
            if (address.root < 0 || address.offset < INT32_MIN
                || address.offset + bytes > INT32_MAX) return false;
            unsigned group = 0;
            while (group < proof_groups.size()
                && (proof_groups[group].root != unsigned(address.root) || proof_groups[group].write != write)) ++group;
            if (group == proof_groups.size())
                proof_groups.push_back({unsigned(address.root), write, address.offset, address.offset + bytes, 0});
            auto &span = proof_groups[group];
            span.low = std::min(span.low, address.offset);
            span.high = std::max(span.high, address.offset + bytes);
            if (span.high - span.low > 4096) return false;
            proof_accesses.push_back({pc, group, address.offset});
            return true;
        };
        for (std::size_t n = 0; prove_memory && n < instructions.size(); ++n) {
            const auto &ins = instructions[n];
            const auto op = ins.opcode;
            const bool last = n + 1 == instructions.size();
            if (ins.leaf || ins.offset != n * 4 || (op >> 28) != 14) { prove_memory = false; break; }
            const unsigned rn = (op >> 16) & 15, rd = (op >> 12) & 15;
            if ((op & 0x0ffffff0u) == 0x012fff10u) { prove_memory = last; continue; }
            if (((op >> 26) & 3) == 1) {
                const bool pre = op & (1u << 24), up = op & (1u << 23);
                const bool writeback = !pre || (op & (1u << 21)), load = op & (1u << 20);
                if ((op & ((1u << 25) | (1u << 22))) || rn == 15 || rd == 15
                    || (!pre && (op & (1u << 21))) || (writeback && rn == rd)) { prove_memory = false; break; }
                const auto delta = (up ? 1 : -1) * std::int64_t(op & 4095);
                auto address = values[rn]; if (pre) address.offset += delta;
                prove_memory = add_access(ins.address, address, 4, !load);
                if (writeback) values[rn].offset += delta;
                if (load) values[rd] = {};
            } else if (((op >> 25) & 7) == 4) {
                const bool load = op & (1u << 20), writeback = op & (1u << 21);
                const bool up = op & (1u << 23), pre = op & (1u << 24);
                const auto list = op & 65535;
                if (!list || rn == 15 || (op & (1u << 22))
                    || (writeback && (list & (1u << rn)))
                    || (load && (list & 32768) && !last)) { prove_memory = false; break; }
                unsigned count = 0; for (unsigned r = 0; r < 16; ++r) count += (list >> r) & 1;
                auto address = values[rn];
                address.offset += up ? (pre ? 4 : 0) : -std::int64_t(count * 4) + (pre ? 0 : 4);
                prove_memory = add_access(ins.address, address, count * 4, !load);
                if (writeback) values[rn].offset += (up ? 1 : -1) * std::int64_t(count * 4);
                if (load) for (unsigned r = 0; r < 16; ++r) if (list & (1u << r)) values[r] = {};
            } else if ((op & 0x0f8000f0u) == 0x00800090u) {
                // Long multiply; both halves cease to be entry-relative pointers.
                if (rn == 15 || rd == 15 || rn == rd) { prove_memory = false; break; }
                values[rn] = {}; values[rd] = {};
            } else if ((op & 0x0fc000f0u) == 0x00000090u) {
                if (rn == 15) { prove_memory = false; break; }
                values[rn] = {};
            } else if (((op >> 26) & 3) == 0 && ((op & (1u << 25)) || (op & 0x90) != 0x90)) {
                const unsigned alu = (op >> 21) & 15;
                const bool flags = op & (1u << 20);
                if (alu >= 8 && alu <= 11) { if (!flags) prove_memory = false; continue; }
                if (rd == 15) { prove_memory = last && !flags && alu == 13; continue; }
                affine value;
                if (alu == 13 && !(op & (1u << 25)) && !(op & 0xff0)) value = values[op & 15];
                if ((alu == 2 || alu == 4) && (op & (1u << 25))) {
                    const unsigned rotation = ((op >> 8) & 15) * 2;
                    const std::uint32_t imm = op & 255;
                    const auto immediate = rotation ? (imm >> rotation) | (imm << (32 - rotation)) : imm;
                    value = values[rn]; value.offset += (alu == 4 ? 1 : -1) * std::int64_t(immediate);
                    if (value.offset < INT32_MIN || value.offset > INT32_MAX) value = {};
                }
                values[rd] = value;
            } else prove_memory = false;
        }
        // Separate research policies prove reads, or reads and writes, through
        // unchanged entry registers. They admit loops and conditional accesses.
        // Write proofs also exclude physical code aliases. All helper paths end
        // this region before a later instruction can use a pointer invalidated by a callback.
        const bool include_writes = ir_policy == arm_ir_policy::invariant_writes || (ir_policy == arm_ir_policy::invariant_write_ir || ir_policy == arm_ir_policy::conditional_value_ir)
            || ir_policy == arm_ir_policy::write_budget_chunks || ir_policy == arm_ir_policy::deferred_chunk_counts;
        bool invariant_reads = allow_memory_proof && (ir_policy == arm_ir_policy::invariant_reads || ir_policy == arm_ir_policy::invariant_read_ir || ir_policy == arm_ir_policy::invariant_read_flag_ir || (ir_policy == arm_ir_policy::inline_call_ir || (ir_policy == arm_ir_policy::invariant_write_ir || ir_policy == arm_ir_policy::conditional_value_ir)) || include_writes || ir_policy == arm_ir_policy::budget_chunks)
            && w.region && w.defer_memory && cache_registers && !instructions.empty();
#ifdef EKA2L1_WASM_CODE_VERSIONS
#if defined(EKA2L1_WASM_CODE_WRITE_PROTECTION)
        // Only the original-emitter delivered proof policy is admitted. Its
        // entry checks consume writable TLB tags after runtime protection sync;
        // denied watched pages retain the precise deferred/helper path.
        invariant_reads = invariant_reads && common::code_tracking::protect_writes
            && ir_policy == arm_ir_policy::write_budget_chunks;
#else
        invariant_reads = false;
#endif
#endif
        if (invariant_reads) {
            unsigned written = 0;
            bool known = true;
            for (const auto &ins : instructions) {
                const auto op = ins.opcode;
                const unsigned rn = (op >> 16) & 15, rd = (op >> 12) & 15;
                if ((op >> 28) == 15) { known = false; break; }
                if ((op & 0x0ffffff0u) == 0x012fff10u) continue;
                if (((op >> 25) & 7) == 5) {
                    if (op & (1u << 24)) written |= 1u << 14;
                } else if (((op >> 26) & 3) == 1) {
                    if ((!(op & (1u << 24)) && (op & (1u << 21)))
                        || ((op & (1u << 25)) && (op & 16))) { known = false; break; }
                    if (op & (1u << 20)) written |= 1u << rd;
                    if (!(op & (1u << 24)) || (op & (1u << 21))) written |= 1u << rn;
                } else if (((op >> 25) & 7) == 4) {
                    if ((op & (1u << 22)) || !(op & 65535)) { known = false; break; }
                    if (op & (1u << 20)) written |= op & 65535;
                    if (op & (1u << 21)) written |= 1u << rn;
                } else if ((op & 0x0f8000f0u) == 0x00800090u) {
                    written |= (1u << rn) | (1u << rd);
                } else if ((op & 0x0fc000f0u) == 0x00000090u) {
                    written |= 1u << rn;
                } else if ((op & 0x0e000090u) == 0x00000090u && (op & 0x60)) {
                    // LDRD/STRD use the store encoding space and two outputs;
                    // exclude them rather than treating them as scalar stores.
                    if (!(op & (1u << 20)) && (op & 0x60) != 0x20) { known = false; break; }
                    if (op & (1u << 20)) written |= 1u << rd;
                    if (!(op & (1u << 24)) || (op & (1u << 21))) written |= 1u << rn;
                } else if (((op >> 26) & 3) == 0 && ((op & (1u << 25)) || (op & 0x90) != 0x90)) {
                    const unsigned alu = (op >> 21) & 15;
                    if (alu >= 8 && alu <= 11) { if (!(op & (1u << 20))) known = false; }
                    else {
                        if (rd == 15 && (op & (1u << 20))) known = false;
                        written |= 1u << rd;
                    }
                } else known = false;
                if (!known) break;
            }
            proof_groups.clear(); proof_accesses.clear();
            for (const auto &ins : instructions) {
                if (!known) break;
                const auto op = ins.opcode;
                const unsigned rn = (op >> 16) & 15, rd = (op >> 12) & 15;
                // Immediate pre-indexed word accesses, no writeback or PC operand.
                const bool load = op & (1u << 20);
                if ((op & 0x0f600000u) != 0x05000000u || (!load && !include_writes) || rn == 15 || rd == 15
                    || (written & (1u << rn))) continue;
                known = add_access(ins.address, {int(rn), (op & (1u << 23) ? 1 : -1) * std::int64_t(op & 4095)}, 4, !load);
            }
            prove_memory = known;
        }
        prove_memory = prove_memory && proof_accesses.size() >= 4;
        if (prove_memory && invariant_reads) for (const auto &access : proof_accesses) {
            if (proof_groups[access.group].write) ++tr.proved_writes;
            else ++tr.proved_reads;
        }
        if (prove_memory) {
            for (auto &span : proof_groups) span.host = w.cache.first_local++;
            result.num_locals += static_cast<unsigned>(proof_groups.size());
            for (const auto &access : proof_accesses) {
                const auto &span = proof_groups[access.group];
                w.proved_accesses.emplace(access.pc, arm_emit::proved_access{
                    span.host, static_cast<std::uint32_t>(access.offset - span.low)});
            }
        }

        std::unique_ptr<region_ir> ir;
        std::vector<bool> ir_live;
        std::vector<unsigned> ir_locals;
        if (prove_memory && !invariant_reads) {
            ir = std::make_unique<region_ir>(start_address);
            for (unsigned n = 0; n < instructions.size(); ++n) {
                if (!ir->append(instructions[n].opcode, instructions[n].address,
                        n + 1 == instructions.size(), w.proved_accesses)) {
                    ir.reset(); break;
                }
            }
            if (ir && !ir->valid()) ir.reset();
            // Do not retain the rejected memory-only optimization for regions
            // outside the IR's conservative semantic subset.
            if (!ir) { prove_memory = false; w.proved_accesses.clear(); }
        }
        if (ir) {
            ir_live = ir->live_for({static_cast<unsigned>(ir->snapshots.size() - 1)});
            ir_locals.resize(ir->nodes.size());
            unsigned i32_count = 0, i64_count = 0;
            for (unsigned v = 1; v < ir->nodes.size(); ++v) {
                const auto &node = ir->nodes[v];
                if (!ir_live[v] || node.op == region_ir::constant || node.op == region_ir::host
                    || node.op == region_ir::state || node.type == type_void) continue;
                if (node.type == type_i32) ir_locals[v] = w.cache.first_local + i32_count++;
                else ir_locals[v] = i64_count++;
            }
            w.cache.first_local += i32_count;
            result.num_locals += i32_count;
            result.num_suffix_i64_locals = i64_count;
            // Cache slots precede the i64 suffix. Reserve every state slot that
            // proof/lowering/writeback can use before assigning suffix indices.
            for (unsigned offset = S::AOT_BUDGET; offset <= S::AOT_EXIT; offset += 4) w.cache.local(offset);
            w.cache.local(S::CPSR);
            for (const auto &span : proof_groups) w.cache.local(S::reg(span.root));
            for (unsigned v = 1; v < ir->nodes.size(); ++v)
                if (ir_live[v] && ir->nodes[v].op == region_ir::state)
                    w.cache.local(static_cast<unsigned>(ir->nodes[v].immediate));
            const auto &entry = ir->snapshots.front(); const auto &exit = ir->snapshots.back();
            for (unsigned r = 0; r < 15; ++r)
                if (entry.regs[r] != exit.regs[r]) w.cache.local(S::reg(r));
            for (unsigned f = 0; f < 5; ++f)
                if (entry.flags[f] != exit.flags[f]) w.cache.local(region_ir::flag_offsets[f]);
            const auto suffix = w.cache.first_local + static_cast<unsigned>(w.cache.locals.size());
            for (unsigned v = 1; v < ir->nodes.size(); ++v)
                if (ir_live[v] && ir->nodes[v].type == type_i64 && ir->nodes[v].op != region_ir::constant)
                    ir_locals[v] += suffix;
        }

        // Collect forward branch targets
        std::set<std::uint32_t> forward_targets_set;
        for (std::size_t i : reachable) {
            if (i + 3 >= code_size) continue;
            std::uint32_t inst;
            std::memcpy(&inst, code + i, 4);
            std::uint32_t src = start_address + static_cast<std::uint32_t>(i);

            // B (not BL): bits [27:25] = 101, bit 24 = 0
            if (((inst >> 25) & 7) == 5 && !((inst >> 24) & 1)) {
                std::int32_t offset = inst & 0x00FFFFFF;
                if (offset & 0x00800000) offset |= 0xFF000000;
                offset = (offset << 2) + 8;
                std::uint32_t target = src + static_cast<std::uint32_t>(offset);
                if ((target > src || (region && target != start_address)) && target >= start_address &&
                    target < start_address + code_size) {
                    forward_targets_set.insert(target);
                }
            }
        }

        // A region with one natural loop needs no entry-selector dispatch.
        // Restrict the shape to a single backedge target and no labels inside
        // the loop body; forward exits remain ordinary enclosing blocks.
        bool direct_loop = region
            && code_size <= 0xffffffffu - start_address;
        std::uint32_t loop_start = 0, loop_last = 0;
        bool has_backedge = false;
        for (const auto &instruction : instructions) {
            if (instruction.leaf) continue; // Inlined leaves are straight-line.
            const auto inst = instruction.opcode;
            if (((inst >> 25) & 7) != 5 || ((inst >> 24) & 1)) continue;
            const auto displacement = static_cast<std::int32_t>(inst << 8) >> 6;
            const auto target = instruction.address + 8 + static_cast<std::uint32_t>(displacement);
            if (target > instruction.address || target < start_address
                || target >= start_address + code_size) continue;
            if (has_backedge && target != loop_start) direct_loop = false;
            if (!has_backedge) loop_start = target;
            has_backedge = true;
            loop_last = std::max(loop_last, instruction.address);
        }
        direct_loop = direct_loop && has_backedge;
        for (const auto target : forward_targets_set)
            if (target > loop_start && target <= loop_last) direct_loop = false;
        bool inner_loop_open = false;
        unsigned direct_loop_depth = 0;

        // Populate branch_targets for extra-entry discovery
        for (std::size_t i : reachable) {
            if (i + 3 >= code_size) continue;
            std::uint32_t inst;
            std::memcpy(&inst, code + i, 4);
            std::uint32_t src = start_address + static_cast<std::uint32_t>(i);
            // B/BL targets
            if (((inst >> 25) & 7) == 5) {
                std::int32_t offset = inst & 0x00FFFFFF;
                if (offset & 0x00800000) offset |= 0xFF000000;
                offset = (offset << 2) + 8;
                std::uint32_t target = src + static_cast<std::uint32_t>(offset);
                if (target >= start_address && target < start_address + code_size) {
                    tr.branch_targets.push_back(target);
                }
                // BL return address
                if ((inst >> 24) & 1) {
                    tr.branch_targets.push_back(src + 4);
                }
            }
        }

        if (bounded && !region) forward_targets_set.clear();
        std::vector<std::uint32_t> fwd_sorted(
            forward_targets_set.begin(), forward_targets_set.end());
        std::unordered_map<std::uint32_t, std::uint32_t> fwd_idx;
        for (std::uint32_t k = 0; k < fwd_sorted.size(); k++) {
            fwd_idx[fwd_sorted[k]] = k;
        }

        struct integer_segment {
            region_ir graph;
            std::vector<bool> live, cold, stack_values;
            std::vector<unsigned> locals;
            unsigned length;
            bool memoize_cold = false;
        };
        std::map<std::size_t, integer_segment> segments;
        std::vector<std::pair<std::size_t, unsigned>> wide_fixups;
#ifdef EKA2L1_WASM_IR_SEGMENTS
        if (allow_memory_proof && ir_policy != arm_ir_policy::disabled
            && ir_policy != arm_ir_policy::invariant_reads && ir_policy != arm_ir_policy::invariant_writes && ir_policy != arm_ir_policy::budget_chunks && ir_policy != arm_ir_policy::write_budget_chunks && ir_policy != arm_ir_policy::deferred_chunk_counts && w.region && cache_registers && !ir) {
            unsigned max_locals = 0, max_wide_locals = 0;
#if defined(EKA2L1_WASM_IR_MEMORY) && !defined(EKA2L1_WASM_CODE_VERSIONS)
            const bool dynamic_memory = w.defer_memory;
#else
            const bool dynamic_memory = false;
#endif
            const std::map<std::uint32_t, arm_emit::proved_access> no_memory;
            const auto &segment_memory = (ir_policy == arm_ir_policy::invariant_read_ir || ir_policy == arm_ir_policy::invariant_read_flag_ir || (ir_policy == arm_ir_policy::inline_call_ir || (ir_policy == arm_ir_policy::invariant_write_ir || ir_policy == arm_ir_policy::conditional_value_ir))) ? w.proved_accesses : no_memory;
#ifdef EKA2L1_WASM_IR_OUTLINE
            const bool join_calls = (ir_policy == arm_ir_policy::inline_call_ir || (ir_policy == arm_ir_policy::invariant_write_ir || ir_policy == arm_ir_policy::conditional_value_ir));
#else
            const bool join_calls = false;
#endif
            for (std::size_t first = 0; first < instructions.size();) {
                region_ir graph(instructions[first].address);
                std::size_t end = first;
                for (; end < instructions.size() && end - first < (long_ir_segments ? 128u : 32u); ++end) {
                    const auto &ins = instructions[end];
                    const auto op = ins.opcode;
                    if (end != first) {
                        const auto &previous = instructions[end - 1];
                        const bool linear = ins.leaf == previous.leaf && ins.address == previous.address + 4;
                        const auto callee = inlined.find(previous.offset);
                        const bool enter_leaf = join_calls && !previous.leaf && ins.leaf
                            && callee != inlined.end() && ins.offset == previous.offset && ins.address == callee->second.address;
                        const bool leave_leaf = join_calls && previous.leaf && previous.opcode == 0xe12fff1e
                            && !ins.leaf && ins.address == start_address + previous.offset + 4;
                        if ((!linear && !enter_leaf && !leave_leaf)
                            || (!ins.leaf && forward_targets_set.count(ins.address))) break;
                    }
                    auto trial = graph;
                    if (join_calls && !ins.leaf && inlined.count(ins.offset)) {
                        trial.inline_call(ins.address, inlined.at(ins.offset).address);
                        graph = std::move(trial); continue;
                    }
                    if (join_calls && ins.leaf && op == 0xe12fff1e) {
                        trial.inline_return(start_address + static_cast<std::uint32_t>(ins.offset) + 4);
                        graph = std::move(trial); continue;
                    }
                    // Memory joins only through precise guards; selected policies
                    // also model flags and already validated inline transfers.
                    // Other control transfers remain segment boundaries.
                    if (!dynamic_memory && (((op >> 26) & 3) != 0
                        || (op & 0x0f8000f0u) == 0x00800090u)) break;
                    const bool appended = ir_policy == arm_ir_policy::conditional_value_ir
                        ? trial.append_conditional(op, ins.address, segment_memory, dynamic_memory, true)
                        : trial.append(op, ins.address, false, segment_memory, dynamic_memory, ir_policy == arm_ir_policy::invariant_read_flag_ir || join_calls);
                    if (!appended) break;
                    graph = std::move(trial);
                }
                if (end - first < 3 || !graph.valid()) { ++first; continue; }
                integer_segment segment{std::move(graph), {}, {}, {}, {}, static_cast<unsigned>(end - first)};
                segment.memoize_cold = ir_policy == arm_ir_policy::outlined_recipes || ir_policy == arm_ir_policy::invariant_read_ir || ir_policy == arm_ir_policy::invariant_read_flag_ir || join_calls;
                std::vector<unsigned> exits{segment.length};
                for (const auto &node : segment.graph.nodes)
                    if (node.op == region_ir::guarded_host) exits.push_back(static_cast<unsigned>(node.immediate / 2));
                segment.live = segment.graph.live_for(exits);
                segment.locals.resize(segment.graph.nodes.size());
                segment.cold.resize(segment.graph.nodes.size());
                const auto ordinary = segment.graph.live_for({segment.length});
                unsigned count = 0, wide_count = 0;
                for (unsigned v = 1; v < segment.graph.nodes.size(); ++v) {
                    const auto &node = segment.graph.nodes[v];
                    segment.cold[v] = segment.live[v] && !ordinary[v]
                        && (node.op == region_ir::low || node.op == region_ir::high
                            || (segment.memoize_cold && !region_ir::is_effect(node.op)
                                && node.op != region_ir::constant && node.op != region_ir::state && node.op != region_ir::host));
                }
                segment.stack_values = stack_ir_values
                    ? segment.graph.stack_values_for(segment.live, segment.cold, exits)
                    : std::vector<bool>(segment.graph.nodes.size());
                for (unsigned v = 1; v < segment.graph.nodes.size(); ++v) {
                    const auto &node = segment.graph.nodes[v];
                    if (segment.stack_values[v]) continue;
                    if (segment.live[v] && (!segment.cold[v] || segment.memoize_cold) && node.op != region_ir::state
                        && node.op != region_ir::constant && node.op != region_ir::host && node.type != type_void) {
                        segment.locals[v] = node.type == type_i64 ? wide_count++ : w.cache.first_local + count++;
                    }
                }
                max_locals = std::max(max_locals, count);
                max_wide_locals = std::max(max_wide_locals, wide_count);
                segments.emplace(first, std::move(segment)); first = end;
            }
            // Segments never overlap dynamically, so their value locals can
            // share slots. Reserve them before lazy architectural cache slots.
            w.cache.first_local += max_locals; result.num_locals += max_locals;
            result.num_suffix_i64_locals = max_wide_locals;
        }
#endif

        // Budget chunks keep the existing opcode lowering. Each chunk has one
        // entry and no control transfer. A short budget enters a private precise
        // compiler before effects; the hot path keeps every count/exit check.
        std::map<std::size_t, unsigned> budget_chunks;
        if (allow_memory_proof && (budget_gaps_ir || ir_policy == arm_ir_policy::budget_chunks
                || ir_policy == arm_ir_policy::write_budget_chunks || ir_policy == arm_ir_policy::deferred_chunk_counts)
            && w.region && cache_registers) {
            auto straight = [](std::uint32_t op) {
                if ((op >> 28) == 15) return false;
                const unsigned rn = (op >> 16) & 15, rd = (op >> 12) & 15;
                if (((op >> 26) & 3) == 1)
                    return rd != 15 && rn != 15
                        && !(!(op & (1u << 24)) && (op & (1u << 21)))
                        && !((op & (1u << 25)) && (op & 16));
                if (((op >> 25) & 7) == 4)
                    return rn != 15 && (op & 65535) && !(op & ((1u << 22) | 32768));
                if ((op & 0x0f8000f0u) == 0x00800090u)
                    return rn != 15 && rd != 15 && rn != rd;
                if ((op & 0x0fc000f0u) == 0x00000090u) return rn != 15;
                if ((op & 0x0e000090u) == 0x00000090u && (op & 0x60))
                    return rn != 15 && rd != 15 && ((op & (1u << 20)) || (op & 0x60) == 0x20);
                if (((op >> 26) & 3) != 0 || (!(op & (1u << 25)) && (op & 0x90) == 0x90)) return false;
                const unsigned alu = (op >> 21) & 15;
                return alu >= 8 && alu <= 11 ? bool(op & (1u << 20)) : rd != 15;
            };
            // IR is selected first. A chunk cannot begin within or cross an
            // IR segment, including when that segment takes its cold fallback.
            std::vector<bool> ir_covered(instructions.size());
            for (const auto &part : segments)
                for (unsigned n = 0; n < part.second.length; ++n)
                    ir_covered[part.first + n] = true;
            for (std::size_t first = 0; first < instructions.size();) {
                std::size_t end = first;
                for (; end < instructions.size() && end - first < 32; ++end) {
                    const auto &ins = instructions[end];
                    if (ir_covered[end] || ins.leaf || !straight(ins.opcode)
                        || (end != first && (ins.address != instructions[end - 1].address + 4
                            || forward_targets_set.count(ins.address)))) break;
                }
                if (end - first < 4) { ++first; continue; }
                budget_chunks.emplace(first, static_cast<unsigned>(end - first)); first = end;
            }
        }

        // Initialize pc_idx = 0
        w.i32_const(0);
        w.set_local(PC_IDX);

        if (region) {
            w.i32_const(0); w.set_local(arm_emit::COUNT);
            w.i32_const(-1); w.set_local(arm_emit::READ_PAGE);
            w.i32_const(-1); w.set_local(arm_emit::WRITE_PAGE);
        }

        std::uint32_t proof_call_offset = 0;
        if (prove_memory) {
            // The successful IR path cannot call helpers, raise a memory exit,
            // or cross the scheduler budget. All other cases use the precise
            // original function, before any guest memory/state effect.
            if (!invariant_reads) {
                w.load_i32(S::AOT_BUDGET); w.i32_const(static_cast<unsigned>(instructions.size()));
                w.op(op_i32_lt_u); w.load_i32(S::AOT_EXIT); w.op(op_i32_or);
            } else w.load_i32(S::AOT_EXIT);
            w.set_local(TMP4);
            for (const auto &span : proof_groups) {
                w.load_reg(span.root); w.i32_const(static_cast<std::int32_t>(span.low));
                w.op(op_i32_add); w.set_local(arm_emit::ADDRESS);
                w.block_transfer_host(arm_emit::ADDRESS, static_cast<unsigned>(span.high - span.low), span.write);
                w.get_local(arm_emit::HOST); w.op(op_i32_eqz);
                if (span.write) {
                    // A wrapping physical exclusive end cannot prove non-alias.
                    w.get_local(arm_emit::HOST);
                    w.i32_const(static_cast<std::int32_t>(0xffffffffu - unsigned(span.high - span.low)));
                    w.op(op_i32_gt_u); w.op(op_i32_or);
                    w.get_local(arm_emit::HOST); w.load_i32(S::AOT_CODE_END); w.op(op_i32_lt_u);
                    w.get_local(arm_emit::HOST); w.i32_const(static_cast<std::int32_t>(span.high - span.low)); w.op(op_i32_add);
                    w.load_i32(S::AOT_CODE_BEGIN); w.op(op_i32_gt_u); w.op(op_i32_and); w.op(op_i32_or);
                }
                w.get_local(TMP4); w.op(op_i32_or); w.set_local(TMP4);
                w.get_local(arm_emit::HOST); w.set_local(span.host);
            }
            w.get_local(TMP4); w.op(op_if); w.op(type_void);
            w.state_ptr(); w.op(op_call);
            proof_call_offset = static_cast<std::uint32_t>(result.body.size());
            result.body.insert(result.body.end(), {0x80,0x80,0x80,0x80,0});
            // No guest effects preceded this call. Return directly, bypassing
            // this function's cached-state writeback after the callee updates it.
            w.op(op_return); w.op(op_end);
        }

        if (ir) {
            emit_ir_values(w, *ir, ir_live, ir_locals, true);
            const auto &exit = ir->snapshots.back();
            w.i32_const(exit.count); w.ret();
            std::vector<std::uint8_t> prefix; w.cache.transfer(prefix, true);
            result.outlined_call_offset = proof_call_offset + static_cast<unsigned>(prefix.size()) + 2;
            auto fallback = translate_arm_block_impl(code, code_size, start_address,
                siblings, dll_code, bounded, stop_after_store, cache_registers,
                region, leaves, defer_memory, false);
            fallback.func.export_name += "_ir_fallback";
            result.outlined_callee = std::make_shared<wasm_func_def>(std::move(fallback.func));
            w.cache.finish(result);
            tr.entry_supported = tr.complete = true;
            tr.end_address = start_address + static_cast<unsigned>(instructions.size() * 4);
            tr.bail_count = 1;
            return tr;
        }

        // block $exit
        w.op(op_block); w.op(type_void);
        // loop $loop
        w.op(op_loop); w.op(type_void);
        if (direct_loop && loop_start == start_address) direct_loop_depth = w.scope_depth;
        // Forward target blocks
        for (std::size_t k = 0; k < fwd_sorted.size(); k++) {
            w.op(op_block); w.op(type_void);
        }
        if (region && !direct_loop) {
            // Entry zero starts at the first instruction. Backedges select an
            // interior entry in the same loop without flushing register locals.
            w.op(op_block); w.op(type_void);
            w.get_local(PC_IDX); w.op(op_br_table); leb(result.body,static_cast<std::uint32_t>(fwd_sorted.size()+1));
            for (unsigned n=0;n<=fwd_sorted.size();++n) leb(result.body,n);
            leb(result.body,0);
            w.op(op_end);
        }
        std::uint32_t closed_count = 0;
        const std::uint32_t N_fwd = static_cast<std::uint32_t>(fwd_sorted.size());

        std::uint32_t insn_idx = 0;
        std::uint32_t decoded_end_offset = 0;
        std::size_t segment_end = 0, skip_segment_until = 0, budget_chunk_end = 0;

        for (const auto &instruction : instructions) {
            const auto instruction_index = static_cast<std::size_t>(&instruction - instructions.data());
            if (instruction_index < skip_segment_until) continue;
            // Flush fallthrough before closing labels: a taken edge must not
            // inherit the lexical predecessor's pending instruction count.
            if (instruction_index >= budget_chunk_end) w.commit_count();
            // Opcode emitters commonly continue the outer loop. Close the
            // fallback at the next lexical boundary so all such paths join.
            if (segment_end && segment_end == instruction_index) {
                w.end_wide(); w.op(op_end); segment_end = 0;
            }
            const auto segment = segments.find(instruction_index);
            const auto i = instruction.offset;
            if (!region) insn_idx = static_cast<std::uint32_t>(i / 4);
            if (bounded && !region && stop_after_store && w.memory_write) break;
            const auto inst = instruction.opcode;
            const auto insn_addr = instruction.address;
            w.census_pc=insn_addr;w.census_opcode=inst;
            const auto refusal=exit_census::enabled && !instruction.leaf && refusals.count(i)?refusals.at(i):exit_census::leaf_refusal{};
            w.census_constraint=refusal.constraint;w.census_restriction=refusal.detail;
            w.census_rejected_pc=refusal.pc;w.census_rejected_opcode=refusal.opcode;

            const unsigned wide_hi = (inst >> 16) & 15, wide_lo = (inst >> 12) & 15;
            const bool lazy_multiply = region && cache_registers && !instruction.leaf
                && (inst >> 28) == 14 && ((inst >> 23) & 31) == 1
                && ((inst >> 4) & 15) == 9 && !(inst & (1u << 20))
                && wide_hi != 15 && wide_lo != 15 && wide_hi != wide_lo
                && (inst & 15) != 15 && ((inst >> 8) & 15) != 15;
            bool preserve_wide = lazy_multiply
                && (w.wide_lo < 0 || (w.wide_lo == int(wide_lo) && w.wide_hi == int(wide_hi)));
            if (!instruction.leaf && (inst >> 28) == 14) {
                // These restartable deferred loads either finish directly or
                // exit before effects. No callback can invalidate the value.
                if (w.defer_memory && (inst & 0x0f700000u) == 0x05100000u
                    && wide_hi != 15 && wide_lo != 15
                    && int(wide_lo) != w.wide_lo && int(wide_lo) != w.wide_hi)
                    preserve_wide = true;
                // Ordinary ALU operations do not touch the reserved i64 local.
                if (((inst >> 26) & 3) == 0
                    && ((inst & (1u << 25)) || (inst & 0x90) != 0x90)) {
                    const unsigned alu = (inst >> 21) & 15;
                    const bool compare = alu >= 8 && alu <= 11;
                    if ((compare && (inst & (1u << 20))) || (!compare && wide_lo != 15
                        && int(wide_lo) != w.wide_lo && int(wide_lo) != w.wide_hi))
                        preserve_wide = true;
                }
            }
            // Fallthrough must publish before a label closes; taken edges have
            // already published before their branch. No entry bypasses a value.
            if (!preserve_wide || (!instruction.leaf && forward_targets_set.count(insn_addr)))
                w.end_wide();
            const auto budget_chunk = budget_chunks.find(instruction_index);
            if (segment != segments.end() || budget_chunk != budget_chunks.end()) w.end_wide();

            // Only memory/helper paths can raise AOT_EXIT. Straight-line ALU
            // successors need just their budget guard; join/loop entries retain
            // the exit check independently of their lexical predecessor.
            const bool check_exit = w.instruction_may_exit || insn_addr == start_address
                || (!instruction.leaf && forward_targets_set.count(insn_addr));
            w.instruction_may_exit = false;
            w.current_pc = insn_addr; w.pc_written = false; w.restartable_access = false;

            if (inner_loop_open && !instruction.leaf && insn_addr > loop_last) {
                w.op(op_end);
                inner_loop_open = false;
            }

            // Close forward-target blocks
            while (!instruction.leaf && closed_count < N_fwd && fwd_sorted[closed_count] == insn_addr) {
                w.op(op_end);
                closed_count++;
            }

            if (direct_loop && !instruction.leaf && loop_start != start_address && insn_addr == loop_start) {
                w.op(op_loop); w.op(type_void);
                direct_loop_depth = w.scope_depth;
                inner_loop_open = true;
            }

            if (bounded) {
                if (!region) w.store_i32_const(S::PC, insn_addr);
                const bool check_budget = instruction_index >= budget_chunk_end;
                if (check_budget) {
                    w.load_i32(S::AOT_BUDGET);
                    if (region) w.get_local(arm_emit::COUNT); else w.i32_const(insn_idx);
                    w.op(op_i32_le_u);
                }
                if (region && check_exit) {
                    w.load_i32(S::AOT_EXIT); if (check_budget) w.op(op_i32_or);
                }
                if (check_budget || (region && check_exit)) {
                    w.op(op_if); w.op(type_void);
                    w.bail(insn_addr, insn_idx, exit_census::guard);
                    w.op(op_end);
                }
                if (region) {
                    if (ir_policy == arm_ir_policy::deferred_chunk_counts && instruction_index < budget_chunk_end) {
                        ++w.count_offset; ++tr.deferred_count_updates;
                    } else {
                        w.get_local(arm_emit::COUNT); w.i32_const(1); w.op(op_i32_add); w.set_local(arm_emit::COUNT);
                    }
                }
                // Match DynCom's PLD decode: an optional prefetch hint has no
                // architectural effect, but still consumes one guest instruction.
                if ((inst & 0xFD70F000) == 0xF550F000) {
                    ++insn_idx;
                    decoded_end_offset = static_cast<std::uint32_t>(i) + 4;
                    continue;
                }
                // Exclusive/swap encodings overlap broad data-processing masks.
                // Let DynCom preserve the exclusive monitor and instruction semantics.
                if ((inst & 0x0F0000F0) == 0x01000090 || (inst >> 28) == 15) {
                    w.bail_unsupported(insn_addr, insn_idx);
                    decoded_end_offset = static_cast<std::uint32_t>(i);
                    break;
                }
            }

            if (budget_chunk != budget_chunks.end()) {
                const auto length = budget_chunk->second;
                // The normal entry check charged the first instruction and
                // established a non-wrapping remaining-budget subtraction.
                w.load_i32(S::AOT_BUDGET); w.get_local(arm_emit::COUNT); w.op(op_i32_sub);
                w.i32_const(length - 1); w.op(op_i32_lt_u);
                w.op(op_if); w.op(type_void);
                w.get_local(arm_emit::COUNT); w.i32_const(1); w.op(op_i32_sub); w.set_local(arm_emit::COUNT);
                w.store_i32_const(S::PC, insn_addr);
                w.cache.barrier_at(w.b.size());
                w.state_ptr(); w.load_i32(S::AOT_BUDGET); w.get_local(arm_emit::COUNT); w.op(op_i32_sub);
                w.op(op_i32_store); leb(w.b, 2); leb(w.b, S::AOT_BUDGET);
                w.state_ptr(); w.op(op_call);
                const auto call_offset = static_cast<std::uint32_t>(w.b.size());
                w.b.insert(w.b.end(), {0x80, 0x80, 0x80, 0x80, 0});
                w.get_local(arm_emit::COUNT); w.op(op_i32_add);
                w.state_ptr(); w.load_i32(S::AOT_BUDGET);
                w.op(op_i32_store); leb(w.b, 2); leb(w.b, S::AOT_BUDGET);
                w.op(op_return); w.op(op_end);
                std::vector<std::uint32_t> words;
                for (unsigned n = 0; n < length; ++n)
                    words.push_back(instructions[instruction_index + n].opcode);
                auto precise = translate_arm_block_impl(reinterpret_cast<const std::uint8_t *>(words.data()),
                    words.size() * 4, insn_addr, nullptr, nullptr, true, stop_after_store,
                    true, true, nullptr, defer_memory, false);
                precise.func.export_name += "_budget_short";
                result.outlined_calls.push_back({std::make_shared<wasm_func_def>(std::move(precise.func)), call_offset});
                budget_chunk_end = instruction_index + length;
                ++tr.budget_chunks;
            }

            if (segment != segments.end()) {
                const auto &part = segment->second;
                // The normal first-instruction check established COUNT <=
                // budget and charged that instruction. Subtraction cannot wrap.
                w.load_i32(S::AOT_BUDGET); w.get_local(arm_emit::COUNT); w.op(op_i32_sub);
                w.i32_const(part.length - 1); w.op(op_i32_ge_u);
                w.op(op_if); w.op(type_void);
                emit_ir_values(w, part.graph, part.live, part.locals, false, part.cold, &wide_fixups, part.memoize_cold, part.stack_values);
                w.get_local(arm_emit::COUNT); w.i32_const(part.length - 1);
                w.op(op_i32_add); w.set_local(arm_emit::COUNT);
                ++tr.ir_segments;
                tr.ir_max_segment_length = std::max(tr.ir_max_segment_length, part.length);
                tr.ir_segment_instructions += part.length;
                tr.ir_flag_instructions += part.graph.flag_instructions;
                tr.ir_inline_transfers += part.graph.inline_transfers;
                tr.ir_conditional_instructions += part.graph.conditional_instructions;
                for (unsigned v = 1; v < part.graph.nodes.size(); ++v) {
                    if (part.graph.nodes[v].op == region_ir::guarded_host) ++tr.ir_memory_guards;
                    if (part.graph.nodes[v].op == region_ir::read32
                        && part.graph.nodes[part.graph.nodes[v].a].op == region_ir::host) ++tr.ir_proved_reads;
                    if (part.graph.nodes[v].op == region_ir::write32
                        && part.graph.nodes[part.graph.nodes[v].a].op == region_ir::host) ++tr.ir_proved_writes;
                    if (part.live[v] && part.graph.nodes[v].op == op_i64_mul) ++tr.ir_wide_products;
                    if (part.stack_values[v]) ++tr.ir_stack_values;
                    if (part.cold[v]) {
                        ++tr.ir_cold_values;
                        if (part.graph.nodes[v].op == region_ir::low || part.graph.nodes[v].op == region_ir::high)
                            ++tr.ir_cold_halves;
                    }
                }
                w.op(op_else);
#ifdef EKA2L1_WASM_IR_OUTLINE
                if (ir_policy != arm_ir_policy::inline_segments) {
                    // The callee receives the exact remainder and current guest
                    // state. It exhausts that short budget or exits precisely; no
                    // subsequent caller instruction is executed on this arm.
                    w.get_local(arm_emit::COUNT); w.i32_const(1); w.op(op_i32_sub); w.set_local(arm_emit::COUNT);
                    w.store_i32_const(S::PC, insn_addr);
                    w.cache.barrier_at(w.b.size());
                    w.state_ptr(); w.load_i32(S::AOT_BUDGET); w.get_local(arm_emit::COUNT); w.op(op_i32_sub);
                    w.op(op_i32_store); leb(w.b, 2); leb(w.b, S::AOT_BUDGET);
                    w.state_ptr(); w.op(op_call);
                    const auto call_offset = static_cast<std::uint32_t>(w.b.size());
                    w.b.insert(w.b.end(), {0x80, 0x80, 0x80, 0x80, 0});
                    w.get_local(arm_emit::COUNT); w.op(op_i32_add);
                    // Bypass stale caller writeback after the helper updated guest
                    // state. Only restore the runner's original budget contract.
                    w.state_ptr(); w.load_i32(S::AOT_BUDGET);
                    w.op(op_i32_store); leb(w.b, 2); leb(w.b, S::AOT_BUDGET);
                    w.op(op_return); w.op(op_end);
                    std::vector<std::uint32_t> words;
                    // A flattened caller/leaf sequence is not contiguous guest
                    // code. On short budgets execute its first real instruction
                    // at its real PC and return positive progress to the runner.
                    // Never relabel the flattened words as a contiguous slice.
                    const unsigned precise_length = part.graph.inline_transfers ? 1 : part.length;
                    for (unsigned n = 0; n < precise_length; ++n)
                        words.push_back(instructions[instruction_index + n].opcode);
                    auto precise = translate_arm_block_impl(reinterpret_cast<const std::uint8_t *>(words.data()),
                        words.size() * 4, insn_addr, nullptr, nullptr, true, stop_after_store,
                        true, true, nullptr, defer_memory, false);
                    precise.func.export_name += "_ir_short";
                    result.outlined_calls.push_back({std::make_shared<wasm_func_def>(std::move(precise.func)), call_offset});
                    ++tr.ir_outlined_segments;
                    skip_segment_until = instruction_index + part.length;
                    insn_idx += part.length;
                    decoded_end_offset = static_cast<std::uint32_t>(instructions[skip_segment_until - 1].offset) + 4;
                    continue;
                }
#endif
                segment_end = instruction_index + part.length;
            }

            if (instruction.leaf && inst == 0xe12fff1e) {
                // LR still contains this call's ARM return address. The next
                // emitted instruction is the caller continuation, with no spill.
                ++insn_idx;
                decoded_end_offset = static_cast<std::uint32_t>(i) + 4;
                continue;
            }
            if (!instruction.leaf && inlined.count(i)) {
                w.store_i32_const(S::LR, insn_addr + 4);
                ++insn_idx;
                decoded_end_offset = static_cast<std::uint32_t>(i) + 4;
                continue;
            }

            std::uint32_t cond = (inst >> 28) & 0xF;
            bool handled = true;

            // Emit condition check
            bool cond_opened = emit_cond_check(w, cond, TMP1, TMP2);

            // === B/BL: bits [27:25] = 101 ===
            if (((inst >> 25) & 7) == 5) {
                bool is_link = (inst >> 24) & 1;
                std::int32_t offset = inst & 0x00FFFFFF;
                if (offset & 0x00800000) offset |= 0xFF000000;
                offset = (offset << 2);
                // Target = PC + 8 + offset (ARM pipeline: PC = insn_addr + 8)
                std::uint32_t target = insn_addr + 8 + static_cast<std::uint32_t>(offset);
                std::uint32_t next_pc = insn_addr + 4;

                if (is_link) {
                    // BL: set LR = next_pc, bail to target
                    // Check siblings
                    if (siblings) {
                        auto it = siblings->find(target);
                        if (it != siblings->end()) {
                            w.store_i32_const(S::LR, static_cast<std::int32_t>(next_pc));
                            w.state_ptr();
                            w.op(op_call);
                            leb(result.body, it->second);
                            w.i32_const(static_cast<std::int32_t>(insn_idx + 1));
                            w.op(op_i32_add);
                            w.ret();
                            if (cond_opened) w.op(op_end);
                            tr.resume_points.push_back(next_pc);
                            insn_idx++;
                            decoded_end_offset = static_cast<std::uint32_t>(i) + 4;
                            continue;
                        }
                    }
                    w.store_i32_const(S::LR, static_cast<std::int32_t>(next_pc));
                    w.bail(target, insn_idx + 1);
                    if (cond_opened) w.op(op_end);
                    tr.resume_points.push_back(next_pc);
                    insn_idx++;
                    decoded_end_offset = static_cast<std::uint32_t>(i) + 4;
                    continue;
                }

                // B (not link)
                // Check if target is a forward branch within the block
                auto fit = fwd_idx.find(target);
                if (fit != fwd_idx.end() && target > insn_addr) {
                    // Forward branch: br to the appropriate block depth
                    std::uint32_t depth = fit->second - closed_count + (cond_opened ? 1 : 0) + (inner_loop_open ? 1 : 0);
                    w.op(op_br);
                    leb(result.body, depth);
                    if (cond_opened) w.op(op_end);
                } else if ((!bounded && target >= start_address && target < start_address + code_size) || (region && target >= start_address && target < start_address + code_size && (target == start_address || fit != fwd_idx.end()))) {
                    if (region) {
                        if (!direct_loop) {
                            w.i32_const(target == start_address ? 0 : fit->second+1); w.set_local(PC_IDX);
                        }
                        w.load_i32(S::NIRQ); w.op(op_i32_eqz);
                        w.load_i32(S::CPSR); w.i32_const(0x80); w.op(op_i32_and); w.op(op_i32_eqz);
                        w.op(op_i32_and);
                        w.op(op_if); w.op(type_void); w.bail(target,insn_idx+1,exit_census::interrupt); w.op(op_end);
                    }
                    // Backward branch within block: br to loop
                    std::uint32_t loop_depth = direct_loop ? w.scope_depth - direct_loop_depth
                        : N_fwd - closed_count + (cond_opened ? 1 : 0); // loop is right after blocks
                    w.op(op_br);
                    leb(result.body, loop_depth);
                    if (cond_opened) w.op(op_end);
                } else {
                    // Out of block
                    w.bail(target, insn_idx + 1);
                    if (cond_opened) w.op(op_end);
                    if (cond >= 0xE) {
                        // Unconditional: stop decoding
                        if (closed_count >= N_fwd) {
                            decoded_end_offset = static_cast<std::uint32_t>(i) + 4;
                            insn_idx++;
                            break;
                        }
                    }
                }
                insn_idx++;
                decoded_end_offset = static_cast<std::uint32_t>(i) + 4;
                continue;
            }

            // === BX Rm: 0x012FFF1x ===
            if ((inst & 0x0FFFFFF0) == 0x012FFF10) {
                int rm = inst & 0xF;
                if (rm == 14) {
                    // BX LR — function return
                    w.load_reg(14);
                    w.set_local(TMP1);
                    w.store_reg(15, TMP1);
                    // If target has bit 0 set, switch to Thumb
                    w.get_local(TMP1);
                    w.i32_const(1);
                    w.op(op_i32_and);
                    w.set_local(TMP2);
                    w.store_i32(S::TFLAG, TMP2);
                    w.bail_preserve_pc(insn_idx + 1);
                    if (cond_opened) w.op(op_end);
                    if (cond >= 0xE && closed_count >= N_fwd) {
                        decoded_end_offset = static_cast<std::uint32_t>(i) + 4;
                        insn_idx++;
                        break;
                    }
                } else {
                    // BX Rm — indirect branch
                    w.load_reg(rm);
                    w.set_local(TMP1);
                    w.store_reg(15, TMP1);
                    w.get_local(TMP1);
                    w.i32_const(1);
                    w.op(op_i32_and);
                    w.set_local(TMP2);
                    w.store_i32(S::TFLAG, TMP2);
                    w.bail_preserve_pc(insn_idx + 1);
                    if (cond_opened) w.op(op_end);
                    if (cond >= 0xE && closed_count >= N_fwd) {
                        decoded_end_offset = static_cast<std::uint32_t>(i) + 4;
                        insn_idx++;
                        break;
                    }
                }
                insn_idx++;
                decoded_end_offset = static_cast<std::uint32_t>(i) + 4;
                continue;
            }

            // === BLX Rm: 0x012FFF3x ===
            if ((inst & 0x0FFFFFF0) == 0x012FFF30) {
                int rm = inst & 0xF;
                std::uint32_t next_pc = insn_addr + 4;
                w.store_i32_const(S::LR, static_cast<std::int32_t>(next_pc));
                w.load_reg(rm);
                w.set_local(TMP1);
                w.store_reg(15, TMP1);
                w.get_local(TMP1);
                w.i32_const(1);
                w.op(op_i32_and);
                w.set_local(TMP2);
                w.store_i32(S::TFLAG, TMP2);
                w.bail_preserve_pc(insn_idx + 1);
                if (cond_opened) w.op(op_end);
                tr.resume_points.push_back(next_pc);
                insn_idx++;
                decoded_end_offset = static_cast<std::uint32_t>(i) + 4;
                continue;
            }

            // === LDM/STM: bits [27:25] = 100 ===
            if (((inst >> 25) & 7) == 4) {
                bool load = (inst >> 20) & 1;
                bool writeback = (inst >> 21) & 1;
                bool up = (inst >> 23) & 1;
                bool preindex = (inst >> 24) & 1;
                int rn = (inst >> 16) & 0xF;
                std::uint32_t reglist = inst & 0xFFFF;
                if (bounded && ((inst & (1u << 22)) || !reglist || (writeback && (reglist & (1u << rn))))) {
                    w.bail_unsupported(insn_addr, insn_idx); // banked/SPSR and base-in-list cases
                    if (cond_opened) w.op(op_end);
                    ++insn_idx; decoded_end_offset = static_cast<std::uint32_t>(i) + 4;
                    continue;
                }

                int count = 0;
                for (int r = 0; r < 16; r++) {
                    if (reglist & (1 << r)) count++;
                }
                bool has_pc = (reglist >> 15) & 1;

                // Compute base address
                w.load_reg(rn);
                if (!up) {
                    // Decrement: base = Rn - count*4
                    w.i32_const(count * 4);
                    w.op(op_i32_sub);
                }
                if (preindex) {
                    if (up) {
                        w.i32_const(4);
                        w.op(op_i32_add);
                    }
                    // For !up + preindex, we already subtracted, no extra adjust needed
                    // (DA = Rn - count*4; DB = Rn - count*4 is the same start for IB/DB)
                } else if (!up) {
                    // DA: need to add 4 since we subtracted count*4 but DA starts at Rn-(count-1)*4
                    w.i32_const(4);
                    w.op(op_i32_add);
                }
                w.set_local(TMP1); // base address

                const bool proved_span = w.has_proved_access();
                const bool span_fast_path = w.region && (count >= 2 || proved_span);
                const bool pc_written_before_span = w.pc_written;
                if (span_fast_path) {
                    if (proved_span) w.proved_host();
                    else {
                        w.block_transfer_host(TMP1, count * 4, !load);
                        w.get_local(arm_emit::HOST); w.op(op_if); w.op(type_void);
                    }
                    unsigned offset = 0;
                    for (int r = 0; r < 16; ++r) {
                        if (!(reglist & (1u << r))) continue;
                        w.get_local(arm_emit::HOST);
                        if (load) {
                            w.op(op_i32_load); leb(result.body,2); leb(result.body,offset);
                            w.set_local(TMP2); w.store_reg(r,TMP2);
                        } else {
                            if (r == 15) w.i32_const(insn_addr + 8); else w.load_reg(r);
                            w.op(op_i32_store); leb(result.body,2); leb(result.body,offset);
                            w.memory_write = true;
                        }
                        offset += 4;
                    }
                    if (!load && !proved_span) {
                        w.track_write();
                        w.get_local(arm_emit::HOST); w.load_i32(S::AOT_CODE_END); w.op(op_i32_lt_u);
                        w.get_local(arm_emit::HOST); w.i32_const(count * 4); w.op(op_i32_add);
                        w.load_i32(S::AOT_CODE_BEGIN); w.op(op_i32_gt_u); w.op(op_i32_and);
                        w.op(op_if); w.op(type_void); w.store_i32_const(S::AOT_EXIT,1); w.census_effect(2); w.census_code_guard(count*4); w.op(op_end);
                    }
                    if (!proved_span) w.op(op_else);
                    // Code generation visits both arms; a fast LDM PC store
                    // must not suppress PC publication before fallback helpers.
                    if (!proved_span) w.pc_written = pc_written_before_span;
                    if (!proved_span && w.defer_memory && !writeback && !has_pc) {
                        // DynCom publishes block-transfer writeback before its
                        // callbacks, unlike this compiled/native contract. Keep
                        // writeback forms on the existing helper path.
                        // Whole-span validation precedes every transfer. Restart
                        // only here, never after a partially completed LDM/STM.
                        w.get_local(arm_emit::COUNT); w.i32_const(1); w.op(op_i32_sub); w.set_local(arm_emit::COUNT);
                        w.bail(insn_addr, insn_idx, exit_census::memory);
                    }
                }
                int off = 0;
                for (int r = 0; !proved_span && r < 16; r++) {
                    if (!(reglist & (1 << r))) continue;
                    if (load) {
                        w.state_ptr();
                        w.get_local(TMP1);
                        if (off > 0) { w.i32_const(off); w.op(op_i32_add); }
                        w.call(0); // tlb_read32
                        w.set_local(TMP2);
                        w.store_reg(r, TMP2);
                    } else {
                        if (r == 15) w.i32_const(insn_addr + 8);
                        else w.load_reg(r);
                        w.set_local(TMP2);
                        w.state_ptr();
                        w.get_local(TMP1);
                        if (off > 0) { w.i32_const(off); w.op(op_i32_add); }
                        w.get_local(TMP2);
                        w.call(1); // tlb_write32
                    }
                    off += 4;
                }

                if (span_fast_path && !proved_span) w.op(op_end);

                if (writeback) {
                    w.load_reg(rn);
                    if (up) {
                        w.i32_const(count * 4);
                        w.op(op_i32_add);
                    } else {
                        w.i32_const(count * 4);
                        w.op(op_i32_sub);
                    }
                    w.set_local(TMP2);
                    w.store_reg(rn, TMP2);
                }

                if (load && has_pc) {
                    // Set T flag from loaded PC bit 0
                    w.load_reg(15);
                    w.i32_const(1);
                    w.op(op_i32_and);
                    w.set_local(TMP2);
                    w.store_i32(S::TFLAG, TMP2);
                    w.bail_preserve_pc(insn_idx + 1);
                    if (cond_opened) w.op(op_end);
                    if (cond >= 0xE && closed_count >= N_fwd) {
                        decoded_end_offset = static_cast<std::uint32_t>(i) + 4;
                        insn_idx++;
                        break;
                    }
                } else {
                    if (cond_opened) w.op(op_end);
                }
                insn_idx++;
                decoded_end_offset = static_cast<std::uint32_t>(i) + 4;
                continue;
            }

            // === Data processing: bits [27:26] = 00, not multiply ===
            // Multiply has bits [7:4] = 1001 and bits [27:24] = 0000
            // Miscellaneous has bits [27:23] = 00010 and bit 4 = 0 (MRS/MSR etc.)
            // Halfword load/store has bits [27:25] = 000, bit 7 = 1, bit 4 = 1
            if (((inst >> 26) & 3) == 0) {
                // Check for multiply: bits [27:22] = 000000, bits [7:4] = 1001
                bool is_multiply = ((inst >> 22) & 0x3F) == 0 &&
                                   ((inst >> 4) & 0xF) == 9;
                // Long multiply: bits [27:23] = 00001
                bool is_long_multiply = ((inst >> 23) & 0x1F) == 1 &&
                                        ((inst >> 4) & 0xF) == 9;

                // Halfword/signed load/store: bits [27:25]=000, bit 7=1, bit 4=1
                // but NOT multiply (bits [7:4] != 1001)
                bool is_misc_ls = ((inst >> 25) & 7) == 0 &&
                                  ((inst >> 7) & 1) == 1 &&
                                  ((inst >> 4) & 1) == 1 &&
                                  ((inst >> 4) & 0xF) != 9;

                // MRS/MSR: bits [27:23] = 00010, I=0, S=0
                bool is_mrs_msr = ((inst >> 23) & 0x1F) == 2 &&
                                  !((inst >> 20) & 1) && // TST/TEQ/CMP/CMN have S=1
                                  !((inst >> 25) & 1) &&
                                  ((inst >> 4) & 0xF) == 0;

                if (is_multiply) {
                    // MUL: Rd = Rm * Rs (bits [21]=0)
                    // MLA: Rd = Rm * Rs + Rn (bits [21]=1)
                    int rd = (inst >> 16) & 0xF;
                    int rn = (inst >> 12) & 0xF;
                    int rs = (inst >> 8) & 0xF;
                    int rm = inst & 0xF;
                    bool accumulate = (inst >> 21) & 1;
                    bool set_flags = (inst >> 20) & 1;

                    w.load_reg(rm);
                    w.load_reg(rs);
                    w.op(op_i32_mul);
                    if (accumulate) {
                        w.load_reg(rn);
                        w.op(op_i32_add);
                    }
                    w.set_local(TMP1);
                    w.store_reg(rd, TMP1);
                    if (set_flags) {
                        emit_nz_flags(w, TMP1, TMP2);
                    }
                    if (cond_opened) w.op(op_end);
                    insn_idx++;
                    decoded_end_offset = static_cast<std::uint32_t>(i) + 4;
                    continue;
                }

                if (is_long_multiply) {
                    const unsigned hi = (inst >> 16) & 15, lo = (inst >> 12) & 15;
                    const unsigned rs = (inst >> 8) & 15, rm = inst & 15;
                    const bool signed_product = (inst >> 22) & 1;
                    const bool accumulate = (inst >> 21) & 1, set_flags = (inst >> 20) & 1;
                    // PC operands and identical destination registers are unpredictable.
                    if (hi == 15 || lo == 15 || rs == 15 || rm == 15 || hi == lo) {
                        w.bail_unsupported(insn_addr, insn_idx);
                    } else {
                        // Read every input before either destination is changed (overlap is valid).
                        w.load_reg(rm); w.op(signed_product ? op_i64_extend_i32_s : op_i64_extend_i32_u);
                        w.load_reg(rs); w.op(signed_product ? op_i64_extend_i32_s : op_i64_extend_i32_u);
                        w.op(op_i64_mul);
                        if (accumulate) {
                            if (w.wide_lo == int(lo) && w.wide_hi == int(hi)) w.get_local(WIDE);
                            else {
                                w.load_reg(hi); w.op(op_i64_extend_i32_u);
                                w.op(op_i64_const); w.b.push_back(32); w.op(op_i64_shl);
                                w.load_reg(lo); w.op(op_i64_extend_i32_u); w.op(op_i64_or);
                            }
                            w.op(op_i64_add); // WASM wraps modulo 2^64, including signed overflow.
                        }
                        if (lazy_multiply) {
                            w.set_local(WIDE); w.wide_lo = lo; w.wide_hi = hi;
                        } else {
                        w.tee_local(WIDE); w.op(op_i32_wrap_i64); w.set_local(TMP1);
                        w.get_local(WIDE); w.op(op_i64_const); w.b.push_back(32);
                        w.op(op_i64_shr_u); w.op(op_i32_wrap_i64); w.set_local(TMP2);
                        w.store_reg(lo, TMP1); w.store_reg(hi, TMP2);
                        if (set_flags) {
                            w.get_local(TMP2); w.i32_const(31); w.op(op_i32_shr_u);
                            w.set_local(TMP3); w.store_i32(state_offsets::NFLAG, TMP3);
                            w.get_local(TMP1); w.get_local(TMP2); w.op(op_i32_or); w.op(op_i32_eqz);
                            w.set_local(TMP3); w.store_i32(state_offsets::ZFLAG, TMP3);
                            // C and V are unchanged.
                        }
                        }
                    }
                    if (cond_opened) w.op(op_end);
                    insn_idx++;
                    decoded_end_offset = static_cast<std::uint32_t>(i) + 4;
                    continue;
                }

                if (is_misc_ls) {
                    // LDRH/STRH/LDRSB/LDRSH
                    int rn = (inst >> 16) & 0xF;
                    int rd = (inst >> 12) & 0xF;
                    bool load = (inst >> 20) & 1;
                    bool preindex = (inst >> 24) & 1;
                    bool up = (inst >> 23) & 1;
                    bool writeback = (inst >> 21) & 1;
                    bool imm_offset = (inst >> 22) & 1;
                    std::uint32_t sh = (inst >> 5) & 3;

                    if (bounded && !load && sh != 1) {
                        // LDRD/STRD share this encoding space, not STRH.
                        w.bail_unsupported(insn_addr, insn_idx);
                        if (cond_opened) w.op(op_end);
                        ++insn_idx; decoded_end_offset = static_cast<std::uint32_t>(i) + 4;
                        continue;
                    }
                    // Compute offset
                    if (imm_offset) {
                        std::uint32_t hi = (inst >> 8) & 0xF;
                        std::uint32_t lo = inst & 0xF;
                        std::uint32_t off8 = (hi << 4) | lo;
                        if (rn == 15) {
                            std::uint32_t base = insn_addr + 8;
                            std::uint32_t addr = up ? base + off8 : base - off8;
                            w.i32_const(static_cast<std::int32_t>(addr));
                        } else {
                            w.load_reg(rn);
                            if (off8 > 0) {
                                w.i32_const(static_cast<std::int32_t>(off8));
                                if (up) w.op(op_i32_add); else w.op(op_i32_sub);
                            }
                        }
                    } else {
                        int rm = inst & 0xF;
                        w.load_reg(rn);
                        w.load_reg(rm);
                        if (up) w.op(op_i32_add); else w.op(op_i32_sub);
                    }

                    if (!preindex) {
                        if (writeback || rn == 15 || rd == 15 || rn == rd) {
                            w.op(op_drop);
                            w.bail_unsupported(insn_addr, insn_idx);
                            if (cond_opened) w.op(op_end);
                            ++insn_idx; decoded_end_offset = static_cast<std::uint32_t>(i) + 4;
                            continue;
                        }
                        w.set_local(TMP4); // updated base; keep across the memory callback
                        w.load_reg(rn); // access uses the original base
                    }
                    w.set_local(ADDR_TMP);
                    if (!preindex) w.store_reg(rn, TMP4); // DynCom updates the base before the access.

                    if (load) {
                        w.state_ptr();
                        w.get_local(ADDR_TMP);
                        if (sh == 1) {
                            // LDRH: unsigned halfword
                            if (bounded) w.call(4); // tlb_read16, including access faults
                            else {
                                w.call(2);
                                w.state_ptr(); w.get_local(ADDR_TMP); w.i32_const(1); w.op(op_i32_add);
                                w.call(2); w.i32_const(8); w.op(op_i32_shl); w.op(op_i32_or);
                            }
                        } else if (sh == 2) {
                            // LDRSB: signed byte
                            w.call(2); // tlb_read8
                            // Sign-extend byte
                            w.i32_const(24);
                            w.op(op_i32_shl);
                            w.i32_const(24);
                            w.op(op_i32_shr_s);
                        } else if (sh == 3) {
                            // LDRSH: signed halfword
                            if (bounded) w.call(4); // tlb_read16, including access faults
                            else {
                                w.call(2);
                                w.state_ptr(); w.get_local(ADDR_TMP); w.i32_const(1); w.op(op_i32_add);
                                w.call(2); w.i32_const(8); w.op(op_i32_shl); w.op(op_i32_or);
                            }
                            // Sign-extend halfword
                            w.i32_const(16);
                            w.op(op_i32_shl);
                            w.i32_const(16);
                            w.op(op_i32_shr_s);
                        } else {
                            w.call(0);
                        }
                        w.set_local(TMP1);
                        w.store_reg(rd, TMP1);
                    } else {
                        // STRH
                        w.load_reg(rd);
                        w.set_local(TMP1);
                        w.state_ptr();
                        w.get_local(ADDR_TMP);
                        w.get_local(TMP1);
                        if (bounded) w.call(5); // tlb_write16
                        else {
                            w.call(3);
                            w.state_ptr(); w.get_local(ADDR_TMP); w.i32_const(1); w.op(op_i32_add);
                            w.get_local(TMP1); w.i32_const(8); w.op(op_i32_shr_u); w.call(3);
                        }
                    }

                    if (writeback) {
                        // Write-back for pre-indexed forms.
                        w.store_reg(rn, ADDR_TMP);
                    }

                    if (cond_opened) w.op(op_end);
                    insn_idx++;
                    decoded_end_offset = static_cast<std::uint32_t>(i) + 4;
                    continue;
                }

                if (is_mrs_msr) {
                    // User-mode MSR CPSR_f, Rm: NZCVQ only, no banking or mode change.
                    // Privileged/SPSR/control-field forms stay interpreter-owned.
                    if ((inst & 0x0FFFFFF0) == 0x0128F000 && (inst & 15) != 15) {
                        w.load_i32(S::MODE); w.i32_const(16); w.op(op_i32_ne);
                        // EKA2L1 also uses CPSR mode=0 with active USER32MODE.
                        // ChangePrivilegeMode(0) is a no-op; other mismatches can bank registers.
                        w.load_i32(S::CPSR); w.i32_const(31); w.op(op_i32_and); w.tee_local(TMP1);
                        w.i32_const(0); w.op(op_i32_ne);
                        w.get_local(TMP1); w.i32_const(16); w.op(op_i32_ne);
                        w.op(op_i32_and); w.op(op_i32_or);
                        w.op(op_if); w.op(type_void); if (region) { w.get_local(arm_emit::COUNT); w.i32_const(1); w.op(op_i32_sub); w.set_local(arm_emit::COUNT); } w.bail(insn_addr,insn_idx,exit_census::status); w.op(op_end);
                        w.load_reg(inst & 15); w.set_local(TMP1);
                        for (auto flag : {std::pair<unsigned,unsigned>{S::NFLAG,31},
                                {S::ZFLAG,30},{S::CFLAG,29},{S::VFLAG,28}}) {
                            w.get_local(TMP1); w.i32_const(flag.second); w.op(op_i32_shr_u);
                            w.i32_const(1); w.op(op_i32_and); w.set_local(TMP2); w.store_i32(flag.first,TMP2);
                        }
                        w.load_i32(S::CPSR); w.i32_const(static_cast<std::int32_t>(~0xF8000020u)); w.op(op_i32_and);
                        w.get_local(TMP1); w.i32_const(static_cast<std::int32_t>(0xF8000000u)); w.op(op_i32_and);
                        w.op(op_i32_or); w.set_local(TMP2); w.store_i32(S::CPSR,TMP2);
                    } else w.bail_unsupported(insn_addr, insn_idx);
                    if (cond_opened) w.op(op_end);
                    insn_idx++;
                    decoded_end_offset = static_cast<std::uint32_t>(i) + 4;
                    continue;
                }

                // CLZ: bits [27:20] = 00010110, bits [7:4] = 0001
                if ((inst & 0x0FFF0FF0) == 0x016F0F10) {
                    int rd = (inst >> 12) & 0xF;
                    int rm = inst & 0xF;
                    w.load_reg(rm);
                    w.op(op_i32_clz);
                    w.set_local(TMP1);
                    w.store_reg(rd, TMP1);
                    if (cond_opened) w.op(op_end);
                    insn_idx++;
                    decoded_end_offset = static_cast<std::uint32_t>(i) + 4;
                    continue;
                }

                // Data processing
                std::uint32_t opcode = (inst >> 21) & 0xF;
                bool set_flags = (inst >> 20) & 1;
                int rn = (inst >> 16) & 0xF;
                int rd = (inst >> 12) & 0xF;

                if (bounded && set_flags && rd == 15 && (opcode < 8 || opcode > 11)) {
                    w.bail_unsupported(insn_addr, insn_idx); // SPSR restore is interpreter-owned.
                    if (cond_opened) w.op(op_end);
                    ++insn_idx; decoded_end_offset = static_cast<std::uint32_t>(i) + 4;
                    continue;
                }

                if (bounded && opcode >= 8 && opcode <= 11 && !set_flags) {
                    // Miscellaneous/DSP encodings are not ordinary test opcodes.
                    w.bail_unsupported(insn_addr, insn_idx);
                    if (cond_opened) w.op(op_end);
                    ++insn_idx; decoded_end_offset = static_cast<std::uint32_t>(i) + 4;
                    continue;
                }

                // MOV/MVN ignore Rn. Avoid introducing a guest-state load and
                // cached local for that architecturally unused field.
                if (opcode != 0xD && opcode != 0xF) {
                    // For Rn==15, ARM reads PC+8.
                    if (rn == 15) {
                        w.i32_const(static_cast<std::int32_t>(insn_addr + 8));
                    } else {
                        w.load_reg(rn);
                    }
                    w.set_local(TMP1); // Rn value
                }

                // Get shifter operand
                w.load_i32(S::CFLAG); w.set_local(TMP_CARRY);
                emit_shifter_operand(w, inst, TMP2, TMP3, TMP_CARRY, insn_addr);
                w.set_local(TMP2); // operand 2

                if (set_flags && (opcode == 0 || opcode == 1 || opcode == 8 || opcode == 9 || opcode >= 12))
                    w.store_i32(S::CFLAG, TMP_CARRY);
                // Execute operation
                switch (opcode) {
                case 0x0: // AND
                    w.get_local(TMP1); w.get_local(TMP2);
                    w.op(op_i32_and);
                    w.set_local(TMP3);
                    if (rd != 15) w.store_reg(rd, TMP3);
                    if (set_flags) emit_nz_flags(w, TMP3, TMP4);
                    break;
                case 0x1: // EOR
                    w.get_local(TMP1); w.get_local(TMP2);
                    w.op(op_i32_xor);
                    w.set_local(TMP3);
                    if (rd != 15) w.store_reg(rd, TMP3);
                    if (set_flags) emit_nz_flags(w, TMP3, TMP4);
                    break;
                case 0x2: // SUB
                    w.get_local(TMP1); w.get_local(TMP2);
                    w.op(op_i32_sub);
                    w.set_local(TMP3);
                    if (rd != 15) w.store_reg(rd, TMP3);
                    if (set_flags) {
                        emit_nz_flags(w, TMP3, TMP4);
                        // C = Rn >= op2 (unsigned)
                        w.get_local(TMP1); w.get_local(TMP2);
                        w.op(op_i32_ge_u); w.store_i32_from_stack(S::CFLAG, TMP4);
                        // V = (Rn ^ op2) & (Rn ^ result) >> 31
                        w.get_local(TMP1); w.get_local(TMP2); w.op(op_i32_xor);
                        w.get_local(TMP1); w.get_local(TMP3); w.op(op_i32_xor);
                        w.op(op_i32_and); w.i32_const(31); w.op(op_i32_shr_u);
                        w.store_i32_from_stack(S::VFLAG, TMP4);
                    }
                    break;
                case 0x3: // RSB
                    w.get_local(TMP2); w.get_local(TMP1);
                    w.op(op_i32_sub);
                    w.set_local(TMP3);
                    if (rd != 15) w.store_reg(rd, TMP3);
                    if (set_flags) {
                        emit_nz_flags(w, TMP3, TMP4);
                        // C = op2 >= Rn
                        w.get_local(TMP2); w.get_local(TMP1);
                        w.op(op_i32_ge_u); w.store_i32_from_stack(S::CFLAG, TMP4);
                        // V = (op2 ^ Rn) & (op2 ^ result) >> 31
                        w.get_local(TMP2); w.get_local(TMP1); w.op(op_i32_xor);
                        w.get_local(TMP2); w.get_local(TMP3); w.op(op_i32_xor);
                        w.op(op_i32_and); w.i32_const(31); w.op(op_i32_shr_u);
                        w.store_i32_from_stack(S::VFLAG, TMP4);
                    }
                    break;
                case 0x4: // ADD
                    w.get_local(TMP1); w.get_local(TMP2);
                    w.op(op_i32_add);
                    w.set_local(TMP3);
                    if (rd != 15) w.store_reg(rd, TMP3);
                    if (set_flags) {
                        emit_nz_flags(w, TMP3, TMP4);
                        // C = result < Rn (unsigned overflow)
                        w.get_local(TMP3); w.get_local(TMP1);
                        w.op(op_i32_lt_u); w.store_i32_from_stack(S::CFLAG, TMP4);
                        // V = (Rn ^ result) & (op2 ^ result) >> 31
                        w.get_local(TMP1); w.get_local(TMP3); w.op(op_i32_xor);
                        w.get_local(TMP2); w.get_local(TMP3); w.op(op_i32_xor);
                        w.op(op_i32_and); w.i32_const(31); w.op(op_i32_shr_u);
                        w.store_i32_from_stack(S::VFLAG, TMP4);
                    }
                    break;
                case 0x5: // ADC
                    w.get_local(TMP1); w.get_local(TMP2);
                    w.op(op_i32_add);
                    w.load_i32(S::CFLAG);
                    w.op(op_i32_add);
                    w.set_local(TMP3);
                    if (rd != 15) w.store_reg(rd, TMP3);
                    if (set_flags) {
                        emit_nz_flags(w, TMP3, TMP4);
                        w.get_local(TMP3); w.get_local(TMP1); w.op(op_i32_lt_u);
                        w.get_local(TMP3); w.get_local(TMP1); w.op(op_i32_eq);
                        w.load_i32(S::CFLAG); w.op(op_i32_and); w.op(op_i32_or);
                        w.store_i32_from_stack(S::CFLAG, TMP4);
                        w.get_local(TMP1); w.get_local(TMP3); w.op(op_i32_xor);
                        w.get_local(TMP2); w.get_local(TMP3); w.op(op_i32_xor); w.op(op_i32_and);
                        w.i32_const(31); w.op(op_i32_shr_u); w.store_i32_from_stack(S::VFLAG, TMP4);
                    }
                    break;
                case 0x6: // SBC
                    w.get_local(TMP1); w.get_local(TMP2);
                    w.op(op_i32_sub);
                    w.load_i32(S::CFLAG);
                    w.op(op_i32_eqz); // NOT C
                    w.op(op_i32_sub);
                    w.set_local(TMP3);
                    if (rd != 15) w.store_reg(rd, TMP3);
                    if (set_flags) {
                        emit_nz_flags(w, TMP3, TMP4);
                        w.get_local(TMP1); w.get_local(TMP2); w.op(op_i32_gt_u);
                        w.get_local(TMP1); w.get_local(TMP2); w.op(op_i32_eq);
                        w.load_i32(S::CFLAG); w.op(op_i32_and); w.op(op_i32_or);
                        w.store_i32_from_stack(S::CFLAG, TMP4);
                        w.get_local(TMP1); w.get_local(TMP2); w.op(op_i32_xor);
                        w.get_local(TMP1); w.get_local(TMP3); w.op(op_i32_xor); w.op(op_i32_and);
                        w.i32_const(31); w.op(op_i32_shr_u); w.store_i32_from_stack(S::VFLAG, TMP4);
                    }
                    break;
                case 0x7: // RSC
                    w.get_local(TMP2); w.get_local(TMP1);
                    w.op(op_i32_sub);
                    w.load_i32(S::CFLAG);
                    w.op(op_i32_eqz);
                    w.op(op_i32_sub);
                    w.set_local(TMP3);
                    if (rd != 15) w.store_reg(rd, TMP3);
                    if (set_flags) {
                        emit_nz_flags(w, TMP3, TMP4);
                        w.get_local(TMP2); w.get_local(TMP1); w.op(op_i32_gt_u);
                        w.get_local(TMP2); w.get_local(TMP1); w.op(op_i32_eq);
                        w.load_i32(S::CFLAG); w.op(op_i32_and); w.op(op_i32_or);
                        w.store_i32_from_stack(S::CFLAG, TMP4);
                        w.get_local(TMP2); w.get_local(TMP1); w.op(op_i32_xor);
                        w.get_local(TMP2); w.get_local(TMP3); w.op(op_i32_xor); w.op(op_i32_and);
                        w.i32_const(31); w.op(op_i32_shr_u); w.store_i32_from_stack(S::VFLAG, TMP4);
                    }
                    break;
                case 0x8: // TST (always sets flags, no Rd write)
                    w.get_local(TMP1); w.get_local(TMP2);
                    w.op(op_i32_and);
                    w.set_local(TMP3);
                    emit_nz_flags(w, TMP3, TMP4);
                    break;
                case 0x9: // TEQ
                    w.get_local(TMP1); w.get_local(TMP2);
                    w.op(op_i32_xor);
                    w.set_local(TMP3);
                    emit_nz_flags(w, TMP3, TMP4);
                    break;
                case 0xA: // CMP (SUB without write)
                    w.get_local(TMP1); w.get_local(TMP2);
                    w.op(op_i32_sub);
                    w.set_local(TMP3);
                    emit_nz_flags(w, TMP3, TMP4);
                    // C = Rn >= op2
                    w.get_local(TMP1); w.get_local(TMP2);
                    w.op(op_i32_ge_u); w.store_i32_from_stack(S::CFLAG, TMP4);
                    // V
                    w.get_local(TMP1); w.get_local(TMP2); w.op(op_i32_xor);
                    w.get_local(TMP1); w.get_local(TMP3); w.op(op_i32_xor);
                    w.op(op_i32_and); w.i32_const(31); w.op(op_i32_shr_u);
                    w.store_i32_from_stack(S::VFLAG, TMP4);
                    break;
                case 0xB: // CMN (ADD without write)
                    w.get_local(TMP1); w.get_local(TMP2);
                    w.op(op_i32_add);
                    w.set_local(TMP3);
                    emit_nz_flags(w, TMP3, TMP4);
                    // C = result < Rn
                    w.get_local(TMP3); w.get_local(TMP1);
                    w.op(op_i32_lt_u); w.store_i32_from_stack(S::CFLAG, TMP4);
                    // V = ~(Rn ^ op2) & (Rn ^ result) >> 31
                    w.get_local(TMP1); w.get_local(TMP2); w.op(op_i32_xor);
                    w.i32_const(-1); w.op(op_i32_xor);
                    w.get_local(TMP1); w.get_local(TMP3); w.op(op_i32_xor);
                    w.op(op_i32_and); w.i32_const(31); w.op(op_i32_shr_u);
                    w.store_i32_from_stack(S::VFLAG, TMP4);
                    break;
                case 0xC: // ORR
                    w.get_local(TMP1); w.get_local(TMP2);
                    w.op(op_i32_or);
                    w.set_local(TMP3);
                    if (rd != 15) w.store_reg(rd, TMP3);
                    if (set_flags) emit_nz_flags(w, TMP3, TMP4);
                    break;
                case 0xD: // MOV (Rd = op2, Rn ignored)
                    w.get_local(TMP2);
                    w.set_local(TMP3);
                    if (rd != 15) w.store_reg(rd, TMP3);
                    if (set_flags) emit_nz_flags(w, TMP3, TMP4);
                    break;
                case 0xE: // BIC (Rd = Rn & ~op2)
                    w.get_local(TMP2);
                    w.i32_const(-1);
                    w.op(op_i32_xor); // ~op2
                    w.get_local(TMP1);
                    w.op(op_i32_and);
                    w.set_local(TMP3);
                    if (rd != 15) w.store_reg(rd, TMP3);
                    if (set_flags) emit_nz_flags(w, TMP3, TMP4);
                    break;
                case 0xF: // MVN (Rd = ~op2)
                    w.get_local(TMP2);
                    w.i32_const(-1);
                    w.op(op_i32_xor);
                    w.set_local(TMP3);
                    if (rd != 15) w.store_reg(rd, TMP3);
                    if (set_flags) emit_nz_flags(w, TMP3, TMP4);
                    break;
                }

                // If Rd == 15 and this isn't a compare/test op
                if (rd == 15 && opcode != 0x8 && opcode != 0x9 &&
                    opcode != 0xA && opcode != 0xB) {
                    w.store_reg(15, TMP3);
                    // Data-processing writes to PC keep the instruction mode;
                    // only interworking branches/loads derive T from bit zero.
                    w.bail_preserve_pc(insn_idx + 1);
                    if (cond_opened) w.op(op_end);
                    if (cond >= 0xE && closed_count >= N_fwd) {
                        decoded_end_offset = static_cast<std::uint32_t>(i) + 4;
                        insn_idx++;
                        break;
                    }
                    insn_idx++;
                    decoded_end_offset = static_cast<std::uint32_t>(i) + 4;
                    continue;
                }

                if (cond_opened) w.op(op_end);
                insn_idx++;
                decoded_end_offset = static_cast<std::uint32_t>(i) + 4;
                continue;
            }

            // === Single data transfer (LDR/STR/LDRB/STRB): bits [27:26] = 01 ===
            if (((inst >> 26) & 3) == 1) {
                bool I = (inst >> 25) & 1; // 0=immediate offset, 1=register offset
                bool preindex = (inst >> 24) & 1;
                bool up = (inst >> 23) & 1;
                bool byte = (inst >> 22) & 1;
                bool writeback = (inst >> 21) & 1;
                bool load = (inst >> 20) & 1;
                int rn = (inst >> 16) & 0xF;
                int rd = (inst >> 12) & 0xF;

                if (bounded && I && (inst & 16)) {
                    w.bail_unsupported(insn_addr, insn_idx); // media/undefined encodings
                    if (cond_opened) w.op(op_end);
                    ++insn_idx; decoded_end_offset = static_cast<std::uint32_t>(i) + 4;
                    continue;
                }
                // Compute offset
                if (!I) {
                    // Immediate offset (12-bit)
                    std::uint32_t imm12 = inst & 0xFFF;
                    if (rn == 15) {
                        std::uint32_t base = insn_addr + 8;
                        std::uint32_t addr = up ? base + imm12 : base - imm12;
                        w.i32_const(static_cast<std::int32_t>(addr));
                    } else {
                        w.load_reg(rn);
                        if (imm12 > 0) {
                            w.i32_const(static_cast<std::int32_t>(imm12));
                            if (up) w.op(op_i32_add); else w.op(op_i32_sub);
                        }
                    }
                } else {
                    // Register offset with shift
                    int rm = inst & 0xF;
                    std::uint32_t shift_type = (inst >> 5) & 3;
                    std::uint32_t shift_imm = (inst >> 7) & 0x1F;

                    w.load_reg(rm);
                    // Apply shift
                    if (shift_imm > 0 || shift_type != 0) {
                        switch (shift_type) {
                        case 0: // LSL
                            if (shift_imm > 0) {
                                w.i32_const(static_cast<std::int32_t>(shift_imm));
                                w.op(op_i32_shl);
                            }
                            break;
                        case 1: // LSR
                            if (shift_imm == 0) shift_imm = 32;
                            if (shift_imm == 32) {
                                w.op(op_drop);
                                w.i32_const(0);
                            } else {
                                w.i32_const(static_cast<std::int32_t>(shift_imm));
                                w.op(op_i32_shr_u);
                            }
                            break;
                        case 2: // ASR
                            if (shift_imm == 0) shift_imm = 32;
                            w.i32_const(shift_imm >= 32 ? 31 : static_cast<std::int32_t>(shift_imm));
                            w.op(op_i32_shr_s);
                            break;
                        case 3: // ROR
                            if (shift_imm == 0) {
                                // RRX
                                w.set_local(TMP3);
                                w.load_i32(S::CFLAG);
                                w.i32_const(31);
                                w.op(op_i32_shl);
                                w.get_local(TMP3);
                                w.i32_const(1);
                                w.op(op_i32_shr_u);
                                w.op(op_i32_or);
                            } else {
                                w.i32_const(static_cast<std::int32_t>(shift_imm));
                                w.op(op_i32_rotr);
                            }
                            break;
                        }
                    }

                    w.set_local(TMP3); // offset value
                    if (rn == 15) {
                        w.i32_const(static_cast<std::int32_t>(insn_addr + 8));
                    } else {
                        w.load_reg(rn);
                    }
                    w.get_local(TMP3);
                    if (up) w.op(op_i32_add); else w.op(op_i32_sub);
                }

                if (!preindex) {
                    // W=1 post-indexed forms use unprivileged access semantics.
                    // Keep them and unpredictable register combinations in DynCom.
                    if (writeback || rn == 15 || (rd == 15 && (!load || byte)) || rn == rd) {
                        w.op(op_drop);
                        w.bail_unsupported(insn_addr, insn_idx);
                        if (cond_opened) w.op(op_end);
                        ++insn_idx; decoded_end_offset = static_cast<std::uint32_t>(i) + 4;
                        continue;
                    }
                    w.set_local(TMP4);
                    w.load_reg(rn);
                }
                w.set_local(ADDR_TMP);
                if (!preindex) w.store_reg(rn, TMP4);
                // Post-indexed forms have already published base writeback.
                // Keep all writeback and PC-result forms on the original helper
                // path. The remaining forms have changed only temporaries.
                w.restartable_access = preindex && !writeback && rd != 15;

                if (load) {
                    if (byte) {
                        w.state_ptr();
                        w.get_local(ADDR_TMP);
                        w.call(2); // tlb_read8
                    } else {
                        w.state_ptr();
                        w.get_local(ADDR_TMP);
                        w.call(0); // tlb_read32
                    }
                    w.set_local(TMP1);
                    if (rd == 15) {
                        w.store_reg(15, TMP1);
                        w.get_local(TMP1);
                        w.i32_const(1);
                        w.op(op_i32_and);
                        w.set_local(TMP2);
                        w.store_i32(S::TFLAG, TMP2);
                        if (writeback && rn != 15) {
                            w.store_reg(rn, ADDR_TMP);
                        }
                        w.bail_preserve_pc(insn_idx + 1);
                        if (cond_opened) w.op(op_end);
                        if (cond >= 0xE && closed_count >= N_fwd) {
                            decoded_end_offset = static_cast<std::uint32_t>(i) + 4;
                            insn_idx++;
                            break;
                        }
                        insn_idx++;
                        decoded_end_offset = static_cast<std::uint32_t>(i) + 4;
                        continue;
                    }
                    w.store_reg(rd, TMP1);
                } else {
                    // Store
                    w.load_reg(rd);
                    w.set_local(TMP1);
                    w.state_ptr();
                    w.get_local(ADDR_TMP);
                    w.get_local(TMP1);
                    if (byte) {
                        w.call(3); // tlb_write8
                    } else {
                        w.call(1); // tlb_write32
                    }
                }

                if (writeback && rn != 15) {
                    w.store_reg(rn, ADDR_TMP);
                }

                if (cond_opened) w.op(op_end);
                insn_idx++;
                decoded_end_offset = static_cast<std::uint32_t>(i) + 4;
                continue;
            }

            // === Coprocessor / undefined ===
            // bits [27:26] = 11: coprocessor, SWI
            // Fall through to unsupported

            static std::set<std::uint32_t> seen_arm;
            if (seen_arm.insert(inst & 0x0FFFFFFF).second) {
                fprintf(stderr, "AOT: unsupported ARM insn 0x%08X at 0x%08X\n", inst, insn_addr);
            }
            w.bail_unsupported(insn_addr, insn_idx);
            if (cond_opened) w.op(op_end);
            insn_idx++;
            decoded_end_offset = static_cast<std::uint32_t>(i) + 4;
            // Don't break — continue for subsequent instructions that might
            // be reachable via forward branches.
        }

        w.commit_count();
        if (segment_end) { w.end_wide(); w.op(op_end); }
        if (inner_loop_open) w.op(op_end);

        // Close remaining forward blocks
        while (closed_count < N_fwd) {
            w.op(op_end);
            w.bail(fwd_sorted[closed_count], insn_idx);
            closed_count++;
        }

        // End loop and outer block
        w.op(op_end); // end loop
        w.op(op_end); // end block

        // Return total instruction count
        if (bounded) w.bail(start_address + decoded_end_offset, insn_idx, decoded_end_offset>=code_size?exit_census::source_end:exit_census::emission_end);
        else { w.i32_const(num_insns); w.ret(); }

        if (prove_memory) {
            std::vector<std::uint8_t> prefix;
            w.cache.transfer(prefix, true);
            result.outlined_call_offset = proof_call_offset + static_cast<std::uint32_t>(prefix.size())
                + (w.cache.shared_return ? 2 : 0);
            auto fallback = translate_arm_block_impl(code, code_size, start_address,
                siblings, dll_code, bounded, stop_after_store, cache_registers,
                region, leaves, defer_memory, false);
            fallback.func.export_name += "_memory_fallback";
            result.outlined_callee = std::make_shared<wasm_func_def>(std::move(fallback.func));
        }
        const auto wide_base = w.cache.first_local + static_cast<unsigned>(w.cache.locals.size());
        for (const auto &[position, slot] : wide_fixups) {
            unsigned index = wide_base + slot;
            for (unsigned byte = 0; byte < 5; ++byte) {
                w.b[position + byte] = (index & 127) | (byte == 4 ? 0 : 128);
                index >>= 7;
            }
        }
        w.cache.finish(result);
        tr.entry_supported = w.entry_supported;
        tr.complete = !w.unsupported;
        tr.end_address = start_address + decoded_end_offset;
        tr.bail_count = w.bail_count;
        return tr;
    }

    translate_result translate_arm_block(
        const std::uint8_t *code, std::size_t code_size, std::uint32_t start_address,
        const sibling_map *siblings, const code_window *dll_code, bool bounded,
        bool stop_after_store, bool cache_registers, bool region,
        const leaf_resolver *leaves, bool defer_memory, arm_ir_policy ir_policy)
    {
        return translate_arm_block_impl(code, code_size, start_address, siblings,
            dll_code, bounded, stop_after_store, cache_registers, region, leaves,
            defer_memory, true, ir_policy);
    }
}
