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
#include <cpu/aot/aot_runtime.h>
#include <cpu/aot/watchdog.h>
#include <cpu/aot/state_locals.h>
#include <cpu/aot/wasm_cost.h>
#include <cpu/aot/memory_emission.h>
#include <cpu/aot/exit_census.h>
#include <cpu/aot/execution_limits.h>
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
        source_emission source;
        bool region = false;
        bool direct_block_memory = false;
        bool defer_memory = false, restartable_access = false;
        std::uint32_t current_pc = 0;
        bool pc_written = false;
        // Reserved i32 locals for region instruction count and memory fast path.
        // Short blocks do not use a dynamic count; local 9 records a callback.
        static constexpr unsigned M=14;
        static constexpr unsigned CALLBACK=9;
        static constexpr unsigned COUNT=9, ADDRESS=10, VALUE=11, HOST=12, ENTRY=13;
        bool memory_write = false;
        bool direct_memory_used = false;
        bool instruction_may_exit = true;
        bool entry_supported = true;
        bool unsupported = false;
        std::uint32_t bail_count = 0;
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
            source.mark(b.size());
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
            source_scope provenance(source, source_kind::state);
            if (cache.accepts(offset)) { get_local(cache.local(offset)); return; }
            state_ptr();
            op(op_i32_load); leb(b, 2); leb(b, offset);
        }
        void store_i32(std::uint32_t offset, std::uint32_t local) {
            source_scope provenance(source, source_kind::state);
            if (cache.accepts(offset)) {
                get_local(local); set_local(cache.local(offset)); cache.written.insert(offset); return;
            }
            state_ptr();
            get_local(local);
            op(op_i32_store); leb(b, 2); leb(b, offset);
        }
        void store_i32_from_stack(std::uint32_t offset, std::uint32_t scratch) {
            source_scope provenance(source, source_kind::state);
            if (cache.accepts(offset)) {
                set_local(cache.local(offset)); cache.written.insert(offset); return;
            }
            set_local(scratch);
            state_ptr(); get_local(scratch);
            op(op_i32_store); leb(b, 2); leb(b, offset);
        }
        void store_i32_const(std::uint32_t offset, std::int32_t val) {
            source_scope provenance(source, source_kind::state);
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
            source_scope provenance(source, source_kind::helper);
            instruction_may_exit = true;
            if (region && !pc_written) store_i32_const(S::PC,current_pc);
            cache.barrier_at(b.size());
            op(op_call); leb(b, func_idx);
            cache.barrier_at(b.size(), true);
            if(memory_experiment::enabled() && !cache.enabled) direct_memory_setup(*this);
            if (region) {
                store_i32_const(S::AOT_EXIT, 1); census_effect(1);
            }
            if (direct_block_memory) { i32_const(1); set_local(CALLBACK); }
        }
        void tlb_index(unsigned address_local) {
            get_local(address_local); i32_const(12); op(op_i32_shr_u);
            i32_const(r12l1::TLB_ENTRY_MASK); op(op_i32_and); i32_const(4); op(op_i32_shl);
        }
        void call(std::uint32_t func_idx) {
            instruction_may_exit = true;
            const bool write = func_idx == 1 || func_idx == 3 || func_idx == 5;
            if (write) memory_write = true;
            if ((!region && !direct_block_memory) || func_idx > 5
                    || (direct_block_memory && write &&
                        !common::code_tracking::skip_code_write_guards())) {
                slow_call(func_idx); return;
            }
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
                guest_memory_op(*this, write ? op_i32_store : op_i32_load, 2);
                return;
            }
            if (memory_experiment::enabled() && (!write || common::code_tracking::skip_code_write_guards())) {
                direct_access(*this, size, write, [&] {
                    if (defer_memory && restartable_access) {
                        if (!watchdog::enabled) { get_local(COUNT); i32_const(1); op(op_i32_sub); set_local(COUNT); }
                        bail(current_pc, 0, exit_census::memory);
                    } else {
                        state_ptr(); get_local(ADDRESS); if(write) get_local(VALUE);
                        slow_call(func_idx);
                    }
                });
                return;
            }
            // Each access checks the current TLB mapping and permissions.
            op(op_block); op(write ? type_void : type_i32);
            if(memory_experiment::mode) direct_host(*this, ADDRESS, size, write, 1);
            else {
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
            }
            get_local(HOST); op(op_i32_eqz); op(op_if); op(type_void);
            if (defer_memory && restartable_access) {
                if (!watchdog::enabled) { get_local(COUNT); i32_const(1); op(op_i32_sub); set_local(COUNT); }
                bail(current_pc, 0, exit_census::memory);
            } else {
                state_ptr(); get_local(ADDRESS); if(write) get_local(VALUE);
                slow_call(func_idx);
                op(op_br); leb(b,1); // leave result block, preserving helper result
            }
            op(op_end);
            if(!memory_experiment::mode) {
                get_local(HOST); get_local(ADDRESS); i32_const(4095); op(op_i32_and);
                op(op_i32_add); set_local(HOST);
            }
            get_local(HOST);
            if (write) get_local(VALUE);
            guest_memory_op(*this, write ? (size==4 ? op_i32_store : size==2 ? op_i32_store16 : op_i32_store8)
                     : (size==4 ? op_i32_load : size==2 ? op_i32_load16_u : op_i32_load8_u), size==4?2:size==2?1:0);
            if (write) {
                if (!common::code_tracking::skip_code_write_guards()) {
                // Backing-address guard catches writes through guest aliases.
                get_local(HOST); load_i32(S::AOT_CODE_END); op(op_i32_lt_u);
                get_local(HOST); i32_const(size); op(op_i32_add); load_i32(S::AOT_CODE_BEGIN); op(op_i32_gt_u);
                op(op_i32_and); op(op_if); op(type_void);
                store_i32_const(S::AOT_EXIT,1); census_effect(2); census_code_guard(size); op(op_end);
                }
            }
            op(op_end); // result block
        }
        // Validate a whole block-transfer span once. It must be aligned and
        // contained in one permitted TLB page; otherwise use the existing
        // per-access path, including its fault/endian behavior.
        void block_transfer_host(unsigned address_local, unsigned bytes, bool write, unsigned alignment = 4) {
            if(memory_experiment::mode){direct_host(*this,address_local,bytes,write,alignment);return;}
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
        void census_entry_overlap(unsigned bytes) {
            if(!exit_census::enabled || common::code_tracking::skip_code_write_guards())return;
            // Ignore missing mappings and wrapping spans: those cannot be
            // attributed solely to overlap with the protected code interval.
            get_local(HOST);op(op_i32_eqz);op(op_i32_eqz);
            get_local(HOST);i32_const(0xffffffffu-bytes);op(op_i32_le_u);op(op_i32_and);
            get_local(HOST);load_i32(S::AOT_CODE_END);op(op_i32_lt_u);op(op_i32_and);
            get_local(HOST);i32_const(bytes);op(op_i32_add);load_i32(S::AOT_CODE_BEGIN);op(op_i32_gt_u);op(op_i32_and);
            op(op_if);op(type_void);
            const auto counter=static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&exit_census::entry_overlap_count));
            i32_const(counter);op(op_i32_load);leb(b,2);leb(b,0);set_local(ENTRY);
            get_local(ENTRY);i32_const(32);op(op_i32_lt_u);op(op_if);op(type_void);
            get_local(ENTRY);i32_const(3);op(op_i32_shl);
            i32_const(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(exit_census::entry_overlaps)));op(op_i32_add);set_local(VALUE);
            get_local(VALUE);get_local(HOST);op(op_i32_store);leb(b,2);leb(b,0);
            get_local(VALUE);i32_const(bytes);op(op_i32_store);leb(b,2);leb(b,4);op(op_end);
            i32_const(counter);get_local(ENTRY);i32_const(1);op(op_i32_add);op(op_i32_store);leb(b,2);leb(b,0);
            op(op_end);
        }
        void census_entry_other() {
            if(!exit_census::enabled)return;
            // Preserve the boolean proof expression already on the stack.
            set_local(VALUE);get_local(VALUE);op(op_if);op(type_void);
            census_store(&exit_census::entry_other_failure,1);op(op_end);get_local(VALUE);
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
            source_scope provenance(source, source_kind::dispatch);
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
            if (watchdog::enabled) i32_const(why == exit_census::unsupported
                || why == exit_census::memory || why == exit_census::status ? 0 : 1);
            else if (region) get_local(COUNT); else i32_const(static_cast<std::int32_t>(instr_count));
            ret(why);
            bail_count++;
        }

        void bail_preserve_pc(std::uint32_t instr_count) {
            if (watchdog::enabled) i32_const(1);
            else if (region) get_local(COUNT); else i32_const(static_cast<std::int32_t>(instr_count));
            ret();
            bail_count++;
        }

        void bail_unsupported(std::uint32_t pc, std::uint32_t instr_count) {
            if (region && !watchdog::enabled) { get_local(COUNT); i32_const(1); op(op_i32_sub); set_local(COUNT); }
            unsupported = true;
            if (!instr_count) entry_supported = false;
            bail(pc, instr_count, exit_census::unsupported);
        }
    };

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

    static bool is_branch_veneer(const std::vector<std::uint8_t> &bytes) {
        if (bytes.size() != 4) return false;
        std::uint32_t op; std::memcpy(&op, bytes.data(), 4);
        return (op & 0xff000000u) == 0xea000000u;
    }

    static bool ends_in_tail_branch(const std::vector<std::uint8_t> &bytes) {
        if (bytes.size() < 4) return false;
        std::uint32_t op; std::memcpy(&op, bytes.data() + bytes.size() - 4, 4);
        return (op & 0xff000000u) == 0xea000000u;
    }

    static std::vector<std::uint8_t> resolve_tail_prefix(const std::vector<std::uint8_t> &bytes) {
        for (std::size_t n = 0; n < leaf_instruction_limit * 4 && n + 4 <= bytes.size(); n += 4) {
            std::uint32_t op; std::memcpy(&op, bytes.data() + n, 4);
            if ((op & 0xff000000u) == 0xea000000u)
                return n ? std::vector<std::uint8_t>(bytes.begin(), bytes.begin() + n + 4)
                         : std::vector<std::uint8_t>{};
            if ((op >> 28) > 14 || ((op >> 26) & 3) != 0) return {};
            const unsigned alu = (op >> 21) & 15;
            if (alu >= 8 && alu <= 11 && !(op & (1u << 20))) return {};
            if (((op >> 16) & 15) >= 13 || ((op >> 12) & 15) >= 13) return {};
            if (!(op & (1u << 25)) && ((op & 0x90) == 0x90 || (op & 15) >= 13 ||
                ((op & 16) && ((op >> 8) & 15) >= 13))) return {};
        }
        return {};
    }

    static bool is_literal_pc_veneer(const std::vector<std::uint8_t> &bytes) {
        if (bytes.size() != 4) return false;
        std::uint32_t op; std::memcpy(&op, bytes.data(), 4);
        // Unconditional, immediate, pre-indexed word LDR PC from PC, without
        // writeback. The literal remains a runtime memory access, not code.
        return (op & 0xff7ff000u) == 0xe51ff000u;
    }

    static std::vector<std::uint8_t> resolve_returning_leaf(std::vector<std::uint8_t> bytes, std::uint32_t address, const char *&failure, bool allow_predicates, exit_census::leaf_refusal &refusal) {
        std::vector<std::uint32_t> leaf_targets;
        for (std::size_t n = 0; n < leaf_instruction_limit*4 && n + 4 <= bytes.size(); n += 4) {
            std::uint32_t op; std::memcpy(&op, bytes.data() + n, 4);
            if (op == 0xe12fff1e) {
                // The first return must close every accepted forward path.
                if(std::any_of(leaf_targets.begin(),leaf_targets.end(),[&](auto target) {
                    return std::uint64_t(target)>std::uint64_t(address)+n;
                })) {
                    if(exit_census::enabled){refusal.detail=exit_census::forward_target_after_return;refusal.pc=address+static_cast<std::uint32_t>(n);refusal.opcode=op;}
                    return {};
                }
                bytes.resize(n + 4); return bytes;
            }
            const auto group = (op >> 26) & 3;
            const auto predicate = op >> 28;
            if(predicate<=14 && allow_predicates && ((op>>25)&7)==5 && !(op&(1u<<24))) {
                const auto displacement=static_cast<std::int32_t>(op<<8)>>6;
                const auto pc=std::uint64_t(address)+n;
                const auto target=std::int64_t(pc)+8+displacement;
                if(target>std::int64_t(pc) && target<=0xffffffffll &&
                    target<std::int64_t(address)+std::int64_t(std::min(bytes.size(),std::size_t(leaf_instruction_limit*4)))) {
                    leaf_targets.push_back(static_cast<std::uint32_t>(target)); continue;
                }
            }
            if(predicate<=14 && allow_predicates) {
                const unsigned hi=(op>>16)&15,lo=(op>>12)&15,rs=(op>>8)&15,rm=op&15;
                if((op&0x0f8000f0u)==0x00800090u && hi<13 && lo<13 && hi!=lo && rs<13 && rm<13)continue;
                if((op&0x0fc000f0u)==0x00000090u && hi<13 && rs<13 && rm<13 && ((op&(1u<<21)) ? lo<13 : lo==0))continue;
            }
            if(predicate<=14 && allow_predicates && (op&0x0e000090u)==0x00000090u && (op&0x60)) {
                const unsigned rn=(op>>16)&15,rd=(op>>12)&15;
                const bool load=op&(1u<<20),writeback=!(op&(1u<<24)) || (op&(1u<<21));
                // Exclude doubleword, privileged addressing and overlapping
                // load/writeback destinations; retain the normal fault path.
                if(rn<13 && rd<13 && (load || (op&0x60)==0x20) &&
                    ((op&(1u<<22)) || (op&15)<13) &&
                    !(!(op&(1u<<24)) && (op&(1u<<21))) && !(load && writeback && rn==rd))continue;
            }
            // Conditional integer operations already have precise lowering in
            // the original emitter. They do not alter the linear control path;
            // false predicates still consume an instruction. Expanded scalar
            // forms reuse precise lowering; all remaining instructions retain
            // the conservative register and encoding filters below.
            const auto reject = [&](unsigned detail) {
                if(exit_census::enabled){refusal.detail=detail;refusal.pc=address+static_cast<std::uint32_t>(n);refusal.opcode=op;}
                return std::vector<std::uint8_t>{};
            };
            using namespace exit_census;
            if ((op >> 28) != 14 && (!allow_predicates || (op >> 28) == 15 || group > 1))
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

    static std::vector<std::uint8_t> resolve_leaf(const leaf_resolver &resolve, std::uint32_t address, const char *&failure, bool allow_predicates, exit_census::leaf_refusal &refusal) {
        failure="callee_unsupported";
        auto bytes = resolve(address);
        exit_census::probe(address,bytes.data(),bytes.size());
        if(bytes.empty()) {failure="callee_unmapped_or_other_space";return {};}
        const unsigned features = allow_predicates ? leaf_features : 0;
        // The direct target retains the normal precise dispatcher exit. Only
        // the veneer itself is a dependency; no target bytes are assumed.
        if ((features & 32) && bytes.size() >= 4) {
            std::vector<std::uint8_t> first(bytes.begin(), bytes.begin() + 4);
            if (is_branch_veneer(first)) return first;
        }
        if ((features & 128) && bytes.size() >= 4) {
            std::vector<std::uint8_t> first(bytes.begin(), bytes.begin() + 4);
            if (is_literal_pc_veneer(first)) return first;
        }
        // Preserve a complete returning leaf when the current compiler can
        // already inline it; use a terminal prefix only as the fallback.
        auto prefix = (features & 64) ? resolve_tail_prefix(bytes) : std::vector<std::uint8_t>{};
        auto returning = resolve_returning_leaf(std::move(bytes), address, failure, allow_predicates, refusal);
        return returning.empty() ? prefix : returning;
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
                                exit_census::compile_site(start_address+static_cast<std::uint32_t>(i),inst,bytes.empty()?failure:is_literal_pc_veneer(bytes)?"literal_pc_veneer_inlined":is_branch_veneer(bytes)?"branch_veneer_inlined":ends_in_tail_branch(bytes)?"tail_prefix_inlined":"call_inlined");
                                if(exit_census::enabled && bytes.empty())refusals[i].constraint=
                                    std::strcmp(failure,"leaf_instruction_limit")==0?2:
                                    std::strcmp(failure,"callee_unsupported")==0?3:
                                    std::strcmp(failure,"callee_mapping_extent")==0?4:5;
                                if (!bytes.empty()) {
                                    const bool prefix=is_literal_pc_veneer(bytes) || ends_in_tail_branch(bytes);
                                    inlined.emplace(i, code_dependency{address, std::move(bytes)});
                                    // A transfer prefix exits. The original caller continuation
                                    // is reached only after later guest returns, not now.
                                    if(prefix)break;
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
        const code_window *dll_code, bool bounded, bool stop_after_store, bool cache_registers, bool region, const leaf_resolver *leaves, bool defer_memory, bool allow_memory_proof, bool entry_budget_covers_region, arm_ir_policy ir_policy = arm_ir_policy::configured)
    {
        // Policy 17 adds iteration proofs to policy 7's original lowering.
        const bool loop_budgets = ir_policy == arm_ir_policy::loop_budget_chunks;
        if (loop_budgets) ir_policy = arm_ir_policy::write_budget_chunks;
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
        const bool direct_blocks = bounded && cache_registers && !region;
        result.num_locals = memory_experiment::enabled() ? 15 : region || direct_blocks ? 12 : 7;
        result.num_prefix_i64_locals = 1;
        result.num_f32_locals = 0;
        result.num_f64_locals = 0;
        const std::uint32_t WIDE = 1, TMP1 = 2, TMP2 = 3, TMP3 = 4, TMP4 = 5;
        const std::uint32_t PC_IDX = 6, ADDR_TMP = 7;
        // Separate carry local survives N/Z scratch updates.
        const std::uint32_t TMP_CARRY = 8;

        arm_emit w{result.body};
        w.source.marks = &result.sources;
        w.region = region && bounded;
        w.direct_block_memory = direct_blocks;
        w.defer_memory = w.region && defer_memory;
        w.cache.enabled = bounded && cache_registers;
        // Helpers publish/reload this state through the existing barriers;
        // stop and IRQ signals deliberately remain uncached.
        w.cache.runtime_fields = region || direct_blocks;
        w.cache.program_counter = direct_blocks;
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
            if(is_literal_pc_veneer(leaf.bytes) || ends_in_tail_branch(leaf.bytes))tr.resume_points.push_back(start_address+static_cast<std::uint32_t>(i)+4);
            if (std::none_of(tr.dependencies.begin(), tr.dependencies.end(), [&](const auto &d) { return d.address == leaf.address; }))
                tr.dependencies.push_back(leaf);
            for (std::size_t n = 0; n < leaf.bytes.size(); n += 4) {
                std::memcpy(&op, leaf.bytes.data() + n, 4);
                instructions.push_back({i, leaf.address + static_cast<std::uint32_t>(n), op, true});
            }
        }

        // Entry-relative memory proofs retain ordinary instruction lowering.
        // Failed proofs call the original function before any guest effect;
        // writeback and fault ordering remain exact.
        struct affine { int root = -1; std::int64_t offset = 0; };
        struct proof_group { unsigned root; bool write; std::int64_t low, high; unsigned host; };
        struct proof_access { std::uint32_t pc; unsigned group; std::int64_t offset; };
        std::vector<proof_group> proof_groups;
        std::vector<proof_access> proof_accesses;
        bool prove_memory = false;
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

        // Separate research policies prove reads, or reads and writes, through
        // unchanged entry registers. They admit loops and conditional accesses.
        // Write proofs also exclude physical code aliases. All helper paths end
        // this region before a later instruction can use a pointer invalidated by a callback.
        const bool include_writes = ir_policy == arm_ir_policy::invariant_writes
            || ir_policy == arm_ir_policy::write_budget_chunks;
        const bool invariant_reads = allow_memory_proof && (ir_policy == arm_ir_policy::invariant_reads
            || include_writes || ir_policy == arm_ir_policy::budget_chunks)
            && w.region && w.defer_memory && cache_registers && !instructions.empty();
        if (invariant_reads) {
            unsigned written = 0;
            bool known = true;
            bool linear = entry_budget_covers_region && memory_experiment::enabled();
            std::vector<unsigned> written_before;

            for (const auto &ins : instructions) {
                const auto index = static_cast<std::size_t>(&ins - instructions.data());
                written_before.push_back(written);
                linear &= !ins.leaf && (!index || ins.address == instructions[index - 1].address + 4);
                const auto op = ins.opcode;
                const unsigned rn = (op >> 16) & 15, rd = (op >> 12) & 15;
                if ((op >> 28) == 15) { known = false; break; }
                if ((op & 0x0ffffff0u) == 0x012fff10u) {
                    linear &= index + 1 == instructions.size();
                    continue;
                }
                if (((op >> 25) & 7) == 5) {
                    linear = false;
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
                if (index + 1 < instructions.size() && (written & (1u << 15))) linear = false;
                if (!known) break;
            }
            // The full-entry budget covers this straight-line path. An incoming
            // base remains usable through its last address evaluation, including
            // LDR Rn,[Rn,#imm]. Conditional definitions conservatively end it.
            // New spans need four unconditional uses; helpers still exit before
            // any later instruction can consume an invalidated host pointer.
            std::array<unsigned,16> lifetime_uses{};
            if (known && linear) for (std::size_t index=0;index<instructions.size();++index) {
                const auto op=instructions[index].opcode;
                const unsigned rn=(op>>16)&15, rd=(op>>12)&15;
                if ((op&0xff700000u)==0xe5100000u && rn!=15 && rd!=15
                    && !(written_before[index]&(1u<<rn))) ++lifetime_uses[rn];
            }
            proof_groups.clear(); proof_accesses.clear();
            for (const auto &ins : instructions) {
                if (!known) break;
                const auto index=static_cast<std::size_t>(&ins-instructions.data());
                const auto op = ins.opcode;
                const unsigned rn = (op >> 16) & 15, rd = (op >> 12) & 15;
                // Immediate pre-indexed word accesses, no writeback or PC operand.
                const bool load = op & (1u << 20);
                if ((op & 0x0f600000u) != 0x05000000u || (!load && !include_writes) || rn == 15 || rd == 15
                    || ((written & (1u << rn)) && !(linear && load && (op>>28)==14
                        && lifetime_uses[rn]>=4 && !(written_before[index]&(1u<<rn))))) continue;
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
        std::set<std::uint32_t> safepoints;
        for (const auto &instruction : instructions) {
            if (instruction.leaf) continue; // Inlined leaves are straight-line.
            const auto inst = instruction.opcode;
            if (((inst >> 25) & 7) != 5 || ((inst >> 24) & 1)) continue;
            const auto displacement = static_cast<std::int32_t>(inst << 8) >> 6;
            const auto target = instruction.address + 8 + static_cast<std::uint32_t>(displacement);
            if (target > instruction.address || target < start_address
                || target >= start_address + code_size) continue;
            safepoints.insert(target);
            if (has_backedge && target != loop_start) direct_loop = false;
            if (!has_backedge) loop_start = target;
            has_backedge = true;
            loop_last = std::max(loop_last, instruction.address);
        }
        direct_loop = direct_loop && has_backedge;
        for (const auto target : forward_targets_set)
            if (target > loop_start && target <= loop_last) direct_loop = false;
        if (watchdog::enabled && direct_loop && loop_last >= loop_start + 4) {
            // A pure SUBS counter,#1 / BNE loop reaches zero for every 32-bit
            // entry value, including zero after wraparound. No helper, call,
            // alternate branch or other counter write may occur in its body.
            const auto tail = std::find_if(instructions.begin(), instructions.end(),
                [&](const auto &i) { return !i.leaf && i.address == loop_last; });
            bool finite = tail != instructions.end() && tail != instructions.begin()
                && (tail->opcode & 0xff000000u) == 0x1a000000u;
            unsigned counter = 15;
            if (finite) {
                const auto &decrement = *(tail - 1);
                counter = (decrement.opcode >> 16) & 15;
                finite = !decrement.leaf && decrement.address + 4 == loop_last
                    && (decrement.opcode & 0xfff00fffu) == 0xe2500001u
                    && ((decrement.opcode >> 12) & 15) == counter && counter != 15;
            }
            for (const auto &i : instructions) {
                if (!finite || i.address < loop_start || i.address >= loop_last - 4) continue;
                const auto op = i.opcode;
                const auto alu = (op >> 21) & 15;
                finite = !i.leaf && (op >> 28) == 14 && (op & 0x0c100000u) == 0
                    && ((op & (1u << 25)) || !(op & 0x10))
                    && (alu < 5 || alu >= 12) && ((op >> 12) & 15) != counter
                    && ((op >> 12) & 15) != 15;
            }
            if (finite) { safepoints.erase(loop_start); ++tr.proved_terminating_loops; }
        }
        tr.watchdog_safepoints = watchdog::enabled ? safepoints.size() : 0;
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

        // Budget chunks keep the existing opcode lowering. Straight-line spans
        // and selected single-entry loops prove their maximum instruction count.
        // Short budgets use a precise callee; count/exit checks stay in place.
        std::map<std::size_t, unsigned> budget_chunks;
        std::set<std::size_t> loop_budget_chunks;
        if (!watchdog::enabled && !entry_budget_covers_region && allow_memory_proof && (ir_policy == arm_ir_policy::budget_chunks
                || ir_policy == arm_ir_policy::write_budget_chunks)
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
            for (std::size_t first = 0; first < instructions.size();) {
                // A natural loop has no interior entry. Prove its longest
                // iteration once, including conditional exits; each backedge
                // re-enters this proof. Keep deferred-count policies on
                // their existing straight-line chunks.
                if (loop_budgets && direct_loop && instructions[first].address == loop_start) {
                    std::size_t end = first;
                    for (; end < instructions.size() && end - first < 32; ++end) {
                        const auto &ins = instructions[end];
                        if (ins.leaf || ins.address != loop_start + (end - first) * 4) break;
                        const auto op = ins.opcode;
                        const bool branch = ((op >> 25) & 7) == 5 && !(op & (1u << 24)) && (op >> 28) < 15;
                        const auto target = ins.address + 8 + (static_cast<std::int32_t>(op << 8) >> 6);
                        if (ins.address == loop_last) {
                            if (branch && target == loop_start && end - first >= 3) {
                                budget_chunks.emplace(first, static_cast<unsigned>(end - first + 1));
                                loop_budget_chunks.insert(first);
                                ++end;
                            }
                            break;
                        }
                        if (ins.address > loop_last || (!straight(op) && !(branch && (op >> 28) < 14
                            && (target < loop_start || target > loop_last)))) break;
                    }
                    if (loop_budget_chunks.count(first)) { first = end; continue; }
                }
                std::size_t end = first;
                for (; end < instructions.size() && end - first < 32; ++end) {
                    const auto &ins = instructions[end];
                    if (ins.leaf || !straight(ins.opcode)
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

        if ((!watchdog::enabled && region) || direct_blocks) { w.i32_const(0); w.set_local(arm_emit::COUNT); }

        std::uint32_t proof_call_offset = 0;
        if (prove_memory) {
            if(exit_census::enabled) {
                const auto write_spans=static_cast<unsigned>(std::count_if(proof_groups.begin(),proof_groups.end(),[](const auto &span){return span.write;}));
                w.census_store(&exit_census::entry_proof_attempted,1);
                w.census_store(&exit_census::entry_write_spans,write_spans);
                w.census_store(&exit_census::entry_read_spans,static_cast<unsigned>(proof_groups.size())-write_spans);
            }
            // Failed entry proofs call the precise original function before
            // any guest memory/state effect.
            w.load_i32(S::AOT_EXIT);
            w.census_entry_other();
            w.set_local(TMP4);
            for (const auto &span : proof_groups) {
                w.load_reg(span.root); w.i32_const(static_cast<std::int32_t>(span.low));
                w.op(op_i32_add); w.set_local(arm_emit::ADDRESS);
                w.block_transfer_host(arm_emit::ADDRESS, static_cast<unsigned>(span.high - span.low), span.write);
                w.get_local(arm_emit::HOST); w.op(op_i32_eqz);
                if (span.write) {
                    w.census_entry_overlap(static_cast<unsigned>(span.high-span.low));
                    // A wrapping physical exclusive end cannot prove non-alias.
                    w.get_local(arm_emit::HOST);
                    w.i32_const(static_cast<std::int32_t>(0xffffffffu - unsigned(span.high - span.low)));
                    w.op(op_i32_gt_u); w.op(op_i32_or);
                    w.census_entry_other();
                    if (!common::code_tracking::skip_code_write_guards()) {
                    w.get_local(arm_emit::HOST); w.load_i32(S::AOT_CODE_END); w.op(op_i32_lt_u);
                    w.get_local(arm_emit::HOST); w.i32_const(static_cast<std::int32_t>(span.high - span.low)); w.op(op_i32_add);
                    w.load_i32(S::AOT_CODE_BEGIN); w.op(op_i32_gt_u); w.op(op_i32_and); w.op(op_i32_or);
                    }
                } else w.census_entry_other();
                w.get_local(TMP4); w.op(op_i32_or); w.set_local(TMP4);
                w.get_local(arm_emit::HOST); w.set_local(span.host);
            }
            w.get_local(TMP4); w.op(op_if); w.op(type_void);
            w.census_store(&exit_census::entry_proof_failed,1);
            w.state_ptr(); w.op(op_call);
            proof_call_offset = static_cast<std::uint32_t>(result.body.size());
            result.body.insert(result.body.end(), {0x80,0x80,0x80,0x80,0});
            // No guest effects preceded this call. Return directly, bypassing
            // this function's cached-state writeback after the callee updates it.
            w.op(op_return); w.op(op_end);
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
        std::size_t budget_chunk_end = 0;
        std::vector<std::uint32_t> leaf_forward_targets;
        std::size_t leaf_closed = 0;

        for (const auto &instruction : instructions) {
            const auto instruction_index = static_cast<std::size_t>(&instruction - instructions.data());
            const auto i = instruction.offset;
            if (!region) insn_idx = static_cast<std::uint32_t>(i / 4);
            if (bounded && !region && stop_after_store && w.memory_write) break;
            const auto inst = instruction.opcode;
            const auto insn_addr = instruction.address;
            w.source.pc = insn_addr; w.source.kind = source_kind::guest;
            if(instruction.leaf && !instructions[instruction_index-1].leaf) {
                leaf_forward_targets.clear(); leaf_closed=0;
                const auto &callee=inlined.at(i);
                for(std::size_t n=0;n<callee.bytes.size();n+=4) {
                    std::uint32_t op;std::memcpy(&op,callee.bytes.data()+n,4);
                    if(((op>>25)&7)==5 && !(op&(1u<<24)) && !ends_in_tail_branch(callee.bytes)) {
                        const auto displacement=static_cast<std::int32_t>(op<<8)>>6;
                        leaf_forward_targets.push_back(callee.address+static_cast<std::uint32_t>(n)+8+displacement);
                    }
                }
                std::sort(leaf_forward_targets.begin(),leaf_forward_targets.end());
                leaf_forward_targets.erase(std::unique(leaf_forward_targets.begin(),leaf_forward_targets.end()),leaf_forward_targets.end());
                for(std::size_t n=0;n<leaf_forward_targets.size();++n){w.op(op_block);w.op(type_void);}
            }
            const bool leaf_join=instruction.leaf && std::binary_search(leaf_forward_targets.begin(),leaf_forward_targets.end(),insn_addr);
            w.census_pc=insn_addr;w.census_opcode=inst;
            const auto refusal=exit_census::enabled && !instruction.leaf && refusals.count(i)?refusals.at(i):exit_census::leaf_refusal{};
            w.census_constraint=refusal.constraint;
            w.census_restriction=refusal.detail;
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
            if (!preserve_wide || leaf_join || (!instruction.leaf && forward_targets_set.count(insn_addr)))
                w.end_wide();
            const auto budget_chunk = budget_chunks.find(instruction_index);
            if (budget_chunk != budget_chunks.end()) w.end_wide();

            // Only memory/helper paths can raise AOT_EXIT. Straight-line ALU
            // successors need just their budget guard; join/loop entries retain
            // the exit check independently of their lexical predecessor.
            const bool check_exit = w.instruction_may_exit || insn_addr == start_address
                || leaf_join || (!instruction.leaf && forward_targets_set.count(insn_addr));
            w.instruction_may_exit = false;
            w.current_pc = insn_addr; w.pc_written = false; w.restartable_access = false;
            while(instruction.leaf && leaf_closed<leaf_forward_targets.size() && leaf_forward_targets[leaf_closed]==insn_addr) {
                w.op(op_end); ++leaf_closed;
            }

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

            if (watchdog::enabled && !instruction.leaf && safepoints.count(insn_addr)) {
                w.end_wide();
                watchdog::emit_request(w); w.op(op_if); w.op(type_void);
                w.bail(insn_addr, insn_idx, exit_census::guard); w.op(op_end);
            }
            const bool loop_budget_chunk = loop_budget_chunks.count(instruction_index);
            auto emit_budget_chunk = [&](bool charged) {
                const auto length = budget_chunk->second;
                // COUNT never exceeds the budget. Loop proofs run before the
                // first charge, so zero/short budgets enter the precise callee.
                w.load_i32(S::AOT_BUDGET); w.get_local(arm_emit::COUNT); w.op(op_i32_sub);
                w.i32_const(length - (charged ? 1 : 0)); w.op(op_i32_lt_u);
                w.op(op_if); w.op(type_void);
                if (charged) { w.get_local(arm_emit::COUNT); w.i32_const(1); w.op(op_i32_sub); w.set_local(arm_emit::COUNT); }
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
                    true, true, nullptr, defer_memory, false, false,
                    arm_ir_policy::configured);
                precise.func.export_name += "_budget_short";
                result.outlined_calls.push_back({std::make_shared<wasm_func_def>(std::move(precise.func)), call_offset});
                budget_chunk_end = instruction_index + length;
                ++tr.budget_chunks;
            };

            if (bounded) {
                if (direct_blocks && instruction_index && check_exit) {
                    // Complete every access/writeback in the preceding guest
                    // instruction before honoring its callback's stop or IRQ.
                    // Successful direct accesses avoid these runtime loads.
                    w.get_local(arm_emit::CALLBACK); w.op(op_if); w.op(type_void);
                    w.load_i32(S::NUM_INSTRS_TO_EXECUTE);
                    w.load_i32(S::NUM_INSTRS_TO_EXECUTE + 4);
                    w.op(op_i32_or); w.op(op_i32_eqz);
                    w.op(op_if); w.op(type_void);
                    w.bail(insn_addr, insn_idx, exit_census::guard); w.op(op_end);
                    w.load_i32(S::NIRQ); w.op(op_i32_eqz);
                    w.load_i32(S::CPSR); w.i32_const(0x80);
                    w.op(op_i32_and); w.op(op_i32_eqz); w.op(op_i32_and);
                    w.op(op_if); w.op(type_void);
                    w.bail(insn_addr, insn_idx, exit_census::interrupt); w.op(op_end);
                    w.i32_const(0); w.set_local(arm_emit::CALLBACK);
                    w.op(op_end);
                }
                if (!region) w.store_i32_const(S::PC, insn_addr);
                const bool check_budget = !watchdog::enabled && !entry_budget_covers_region && instruction_index >= budget_chunk_end && !loop_budget_chunk;
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
                if (loop_budget_chunk) {
                    emit_budget_chunk(false);
                    ++tr.loop_budget_chunks;
                }
                if (region && !watchdog::enabled) {
                    w.get_local(arm_emit::COUNT); w.i32_const(1); w.op(op_i32_add); w.set_local(arm_emit::COUNT);
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
                if (((inst & 0x0F0000F0) == 0x01000090 || (inst >> 28) == 15)
                    && !(arm_exclusive_memory && supported_exclusive_word(inst))) {
                    w.bail_unsupported(insn_addr, insn_idx);
                    decoded_end_offset = static_cast<std::uint32_t>(i);
                    break;
                }
            }

            if (budget_chunk != budget_chunks.end() && !loop_budget_chunk) emit_budget_chunk(true);

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

            // The generated instruction performs predicate/PC semantics; the
            // outer loop services the trap at its cumulative guest count. Even
            // a false predicate returns this boundary without invoking kernel.
            if (bounded && compiled_svc_enabled && cond < 15 && (inst & 0x0f000000u) == 0x0f000000u) {
                w.load_i32(S::NUM_INSTRS_TO_EXECUTE); w.i32_const(1); w.op(op_i32_eq);
                w.load_i32(S::NUM_INSTRS_TO_EXECUTE + 4); w.op(op_i32_eqz); w.op(op_i32_and); w.op(op_if); w.op(type_void);
                if (region && !watchdog::enabled) { w.get_local(arm_emit::COUNT); w.i32_const(1); w.op(op_i32_sub); w.set_local(arm_emit::COUNT); }
                w.bail(insn_addr, insn_idx); w.op(op_end);
                const auto descriptor = svc_pending | (((insn_addr + 4) & 4095) ? 0 : svc_page_end) | (inst & 0x00ffffffu);
                w.store_i32_const(S::AOT_EXIT, descriptor);
                const bool predicate = emit_cond_check(w, cond, TMP1, TMP2);
                w.store_i32_const(S::AOT_EXIT, descriptor | svc_taken);
                if (predicate) w.op(op_end);
                w.store_i32_const(S::PC, insn_addr + 4);
                if (!watchdog::enabled) {
                    w.state_ptr();
                    if (region) w.get_local(arm_emit::COUNT); else w.i32_const(insn_idx + 1);
                    w.op(op_i32_store); leb(w.b, 2); leb(w.b, S::AOT_SVC_INSTRUCTIONS);
                }
                w.i32_const(0); w.ret(); ++w.bail_count;
                ++insn_idx; decoded_end_offset = static_cast<std::uint32_t>(i) + 4;
                break;
            }

            // Emit condition check
            bool cond_opened = emit_cond_check(w, cond, TMP1, TMP2);

            // Keep the existing monitor, including failed reservations and
            // callback effects. Return after this instruction so the runner
            // rechecks stops, IRQs, mappings and instruction budget.
            if (bounded && arm_exclusive_memory && supported_exclusive_word(inst)) {
                w.store_i32_const(S::PC, insn_addr);
                w.state_ptr(); w.i32_const(inst); w.slow_call(6);
                w.bail_preserve_pc(insn_idx + 1);
                if (cond_opened) w.op(op_end);
                ++insn_idx;
                decoded_end_offset = static_cast<std::uint32_t>(i) + 4;
                break;
            }

            // === B/BL: bits [27:25] = 101 ===
            if (((inst >> 25) & 7) == 5) {
                bool is_link = (inst >> 24) & 1;
                std::int32_t offset = inst & 0x00FFFFFF;
                if (offset & 0x00800000) offset |= 0xFF000000;
                offset = (offset << 2);
                // Target = PC + 8 + offset (ARM pipeline: PC = insn_addr + 8)
                std::uint32_t target = insn_addr + 8 + static_cast<std::uint32_t>(offset);
                std::uint32_t next_pc = insn_addr + 4;

                if(instruction.leaf && !is_link) {
                    if (ends_in_tail_branch(inlined.at(i).bytes)) {
                        // Even an in-window target exits with the real LR/PC.
                        w.bail(target,insn_idx+1);
                        if(cond_opened)w.op(op_end);
                        ++insn_idx;decoded_end_offset=static_cast<std::uint32_t>(i)+4;
                        continue;
                    }
                    const auto target_index=static_cast<std::size_t>(std::lower_bound(leaf_forward_targets.begin(),leaf_forward_targets.end(),target)-leaf_forward_targets.begin());
                    w.op(op_br);leb(result.body,static_cast<unsigned>(target_index-leaf_closed)+(cond_opened?1:0));
                    if(cond_opened)w.op(op_end);
                    ++insn_idx;decoded_end_offset=static_cast<std::uint32_t>(i)+4;
                    continue;
                }

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
                // Short blocks can share one permission/alignment/endian proof
                // across the complete instruction just like connected regions.
                // Mutation-compatible stores keep their callback path.
                const bool short_span = w.direct_block_memory && (load ||
                    common::code_tracking::skip_code_write_guards());
                const bool span_fast_path = (w.region || short_span) && (count >= 2 || proved_span);
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
                            guest_memory_op(w,op_i32_load,2,offset);
                            w.set_local(TMP2); w.store_reg(r,TMP2);
                        } else {
                            if (r == 15) w.i32_const(insn_addr + 8); else w.load_reg(r);
                            guest_memory_op(w,op_i32_store,2,offset);
                            w.memory_write = true;
                        }
                        offset += 4;
                    }
                    if (!load && !proved_span) {
                        if (!common::code_tracking::skip_code_write_guards()) {
                        w.get_local(arm_emit::HOST); w.load_i32(S::AOT_CODE_END); w.op(op_i32_lt_u);
                        w.get_local(arm_emit::HOST); w.i32_const(count * 4); w.op(op_i32_add);
                        w.load_i32(S::AOT_CODE_BEGIN); w.op(op_i32_gt_u); w.op(op_i32_and);
                        w.op(op_if); w.op(type_void); w.store_i32_const(S::AOT_EXIT,1); w.census_effect(2); w.census_code_guard(count*4); w.op(op_end);
                        }
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
                        if (!watchdog::enabled) { w.get_local(arm_emit::COUNT); w.i32_const(1); w.op(op_i32_sub); w.set_local(arm_emit::COUNT); }
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
                        w.op(op_if); w.op(type_void);
                        if (region && !watchdog::enabled) { w.get_local(arm_emit::COUNT); w.i32_const(1); w.op(op_i32_sub); w.set_local(arm_emit::COUNT); }
                        w.bail(insn_addr,insn_idx,exit_census::status); w.op(op_end);
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
                // The entry proof already supplies this complete address.
                // For cached, immediate word accesses without writeback, using
                // its host local and a WASM memory offset removes the guest
                // address calculation and all helper-argument temporaries.
                // The guard and its original fallback are unchanged.
                if (memory_experiment::enabled() && w.has_proved_access() && w.cache.enabled && !I && preindex
                    && !byte && !writeback && rn != 15 && rd != 15) {
                    const auto &access = w.proved_accesses.at(insn_addr);
                    w.get_local(access.host);
                    if (!load) w.load_reg(rd);
                    guest_memory_op(w, load ? op_i32_load : op_i32_store, 2, access.offset);
                    if (load) w.store_i32_from_stack(S::reg(rd), TMP1);
                    else w.memory_write = true;
                    w.instruction_may_exit = false;
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

        finish_memory_locals(w);
        if (prove_memory) {
            std::vector<std::uint8_t> prefix;
            w.cache.transfer(prefix, true);
            result.outlined_call_offset = proof_call_offset + static_cast<std::uint32_t>(prefix.size())
                + (w.cache.shared_return ? 2 : 0);
            auto fallback = translate_arm_block_impl(code, code_size, start_address,
                siblings, dll_code, bounded, stop_after_store, cache_registers,
                region, leaves, defer_memory, false, false,
                arm_ir_policy::configured);
            fallback.func.export_name += "_memory_fallback";
            result.outlined_callee = std::make_shared<wasm_func_def>(std::move(fallback.func));
        }
        w.cache.finish(result);
        tr.entry_supported = w.entry_supported;
        tr.complete = !w.unsupported;
        tr.end_address = start_address + decoded_end_offset;
        tr.bail_count = w.bail_count;
        return tr;
    }

    // A list scan overwrites its comparison flags on each backedge. Keep the
    // node values in locals and reconstruct flags only at an observable exit.
    // Recognize instruction structure and register roles, never an address.
    static bool summarize_list_scan(translate_result &tr, const std::uint8_t *code,
            std::size_t size, std::uint32_t pc) {
        if (!memory_experiment::enabled() || size < 32 || pc > 0xffffffdfu) return false;
        std::uint32_t words[8]; std::memcpy(words, code, sizeof(words));
        if ((words[0] & 0xfff00fff) != 0xe5900000 || (words[1] & 0xfff0fff0) != 0xe1500000
            || (words[2] & 0xfff00fff) != 0xe240000c || (words[3] & 0xfff00000) != 0x18100000
            || (words[4] & 0xff000000) != 0x0a000000 || (words[5] & 0xfff0f000) != 0xe3500000
            || (words[6] & 0xfff00fff) != 0x12100001 || words[7] != 0x0afffff8) return false;
        const unsigned sentinel = (words[0] >> 16) & 15, cursor = (words[0] >> 12) & 15;
        const unsigned object = (words[2] >> 12) & 15, status = (words[5] >> 16) & 15;
        const unsigned payload = (words[6] >> 16) & 15, result = (words[6] >> 12) & 15;
        std::set<unsigned> regs{sentinel, cursor, object, status, payload, result};
        if (regs.size() != 6 || *regs.rbegin() == 15 || status >= payload || payload >= cursor
            || (words[1] & 15) != sentinel || ((words[1] >> 16) & 15) != cursor
            || ((words[2] >> 16) & 15) != cursor || ((words[3] >> 16) & 15) != cursor
            || (words[3] & 65535) != ((1u << status) | (1u << payload) | (1u << cursor))) return false;
        const auto displacement = static_cast<std::int32_t>(words[4] << 8) >> 6;
        const auto empty_pc = pc + 24 + displacement;
        if (empty_pc >= pc && empty_pc < pc + 32) return false;
        const unsigned rotation = ((words[5] >> 8) & 15) * 2;
        const std::uint32_t imm = words[5] & 255;
        const std::uint32_t magic = rotation ? (imm >> rotation) | (imm << (32 - rotation)) : imm;
        std::vector<std::uint8_t> prefix;
        source_marks marks;
        arm_emit w{prefix}; w.source = {&marks, pc, source_kind::helper};
        const unsigned first = tr.func.num_prefix_i64_locals + tr.func.num_locals + 1;
        const unsigned head=first, node=first+1, obj=first+2, stat=first+3, bits=first+4,
            answer=first+5, count=first+6, budget=first+7, address=first+8,
            tmp=first+9, diff=first+10, host=arm_emit::HOST;
        tr.func.num_locals += 11;
        w.op(op_block); w.op(type_void);
        auto original = [&] { w.op(op_br_if); leb(prefix, 0); };
        if (!watchdog::enabled) {
            w.load_i32(S::AOT_BUDGET); w.tee_local(budget); w.i32_const(8); w.op(op_i32_lt_u); original();
        }
        w.load_i32(S::AOT_EXIT); original();
        w.load_i32(S::NIRQ); w.op(op_i32_eqz);
        w.load_i32(S::CPSR); w.i32_const(0x80); w.op(op_i32_and); w.op(op_i32_eqz);
        w.op(op_i32_and); original();
        direct_memory_setup(w);
        w.get_local(arm_emit::M+2); w.op(op_i32_eqz); original();
        auto load = [&](unsigned base, unsigned offset) {
            w.get_local(base); w.op(op_i32_load); leb(prefix, 2); leb(prefix, offset);
        };
        auto span = [&](unsigned bytes, unsigned alignment) {
            direct_host(w, address, bytes, false, alignment);
        };
        w.load_reg(sentinel); w.tee_local(head); w.set_local(address); span(4,1);
        w.get_local(host); w.op(op_i32_eqz); original();
        w.source.pc=pc; w.source.kind=source_kind::guest_memory;
        load(host, 0); w.set_local(node); w.source.kind=source_kind::helper;
        w.load_reg(status); w.set_local(stat); w.load_reg(payload); w.set_local(bits);
        w.load_reg(result); w.set_local(answer);
        if (!watchdog::enabled) { w.i32_const(1); w.set_local(count); }
        auto publish = [&](unsigned next_pc) {
            w.store_reg(cursor,node); w.store_reg(object,obj); w.store_reg(status,stat);
            w.store_reg(payload,bits); w.store_reg(result,answer); w.store_i32_const(S::PC,next_pc);
        };
        auto cmp_flags = [&](unsigned left, unsigned right) {
            w.get_local(left); w.get_local(right); w.op(op_i32_sub); w.tee_local(diff);
            w.i32_const(31); w.op(op_i32_shr_u); w.store_i32_from_stack(S::NFLAG,tmp);
            w.get_local(diff); w.op(op_i32_eqz); w.store_i32_from_stack(S::ZFLAG,tmp);
            w.get_local(left); w.get_local(right); w.op(op_i32_ge_u); w.store_i32_from_stack(S::CFLAG,tmp);
            w.get_local(left); w.get_local(right); w.op(op_i32_xor);
            w.get_local(left); w.get_local(diff); w.op(op_i32_xor); w.op(op_i32_and);
            w.i32_const(31); w.op(op_i32_shr_u); w.store_i32_from_stack(S::VFLAG,tmp);
        };
        auto final_flags = [&](unsigned z) {
            w.store_i32_const(S::NFLAG,0); w.store_i32_const(S::ZFLAG,z);
            w.get_local(stat); w.i32_const(magic); w.op(op_i32_ge_u); w.store_i32_from_stack(S::CFLAG,tmp);
            w.get_local(stat); w.i32_const(magic); w.op(op_i32_xor);
            w.get_local(stat); w.get_local(stat); w.i32_const(magic); w.op(op_i32_sub); w.op(op_i32_xor);
            w.op(op_i32_and); w.i32_const(31); w.op(op_i32_shr_u); w.store_i32_from_stack(S::VFLAG,tmp);
        };
        auto finish_count = [&](unsigned extra) {
            if (watchdog::enabled) w.i32_const(1);
            else { w.get_local(count); if (extra) { w.i32_const(extra); w.op(op_i32_add); } }
            w.ret();
        };
        w.op(op_loop); w.op(type_void);
        w.source.pc=pc+4;
        w.get_local(node); w.i32_const(12); w.op(op_i32_sub); w.set_local(obj);
        w.get_local(node); w.get_local(head); w.op(op_i32_eq);
        w.op(op_if); w.op(type_void);
        publish(empty_pc); w.store_i32_const(S::NFLAG,0); w.store_i32_const(S::ZFLAG,1);
        w.store_i32_const(S::CFLAG,1); w.store_i32_const(S::VFLAG,0); finish_count(4); w.op(op_end);
        w.get_local(node); w.i32_const(8); w.op(op_i32_sub); w.set_local(address); span(12,4);
        w.get_local(host); w.op(op_i32_eqz);
        w.op(op_if); w.op(type_void);
        publish(pc+12); cmp_flags(node,head); finish_count(2); w.op(op_end);
        w.source.pc=pc+12; w.source.kind=source_kind::guest_memory;
        load(host,0); w.set_local(stat); load(host,4); w.set_local(bits); load(host,8); w.set_local(node);
        w.source.kind=source_kind::helper;
        if (!watchdog::enabled) { w.get_local(count); w.i32_const(7); w.op(op_i32_add); w.set_local(count); }
        w.get_local(stat); w.i32_const(magic); w.op(op_i32_ne);
        w.op(op_if); w.op(type_void);
        w.get_local(bits); w.i32_const(1); w.op(op_i32_and); w.tee_local(answer);
        w.op(op_if); w.op(type_void);
        publish(pc+32); final_flags(0); finish_count(0); w.op(op_end); w.op(op_end);
        if (watchdog::enabled) watchdog::emit_request(w);
        else { w.get_local(budget); w.get_local(count); w.op(op_i32_sub); w.i32_const(7); w.op(op_i32_lt_u); }
        w.load_i32(S::NIRQ); w.op(op_i32_eqz);
        w.load_i32(S::CPSR); w.i32_const(0x80); w.op(op_i32_and); w.op(op_i32_eqz);
        w.op(op_i32_and); w.op(op_i32_or);
        w.op(op_if); w.op(type_void);
        publish(pc+4); final_flags(1); finish_count(0); w.op(op_end);
        w.op(op_br); leb(prefix,0); w.op(op_end); w.op(op_end);
        const auto offset=static_cast<unsigned>(prefix.size());
        if(tr.func.outlined_callee)tr.func.outlined_call_offset+=offset;
        for(auto &call:tr.func.outlined_calls)call.call_offset+=offset;
        for(auto &mark:tr.func.sources)mark.offset+=offset;
        tr.func.sources.insert(tr.func.sources.begin(),marks.begin(),marks.end());
        prefix.insert(prefix.end(),tr.func.body.begin(),tr.func.body.end());tr.func.body=std::move(prefix);
        tr.resume_points.push_back(pc+32);tr.summarized_helpers=1;
        return true;
    }

    // Recognize this complete signed divmod algorithm by bytes, independently
    // of its address. Only the out-of-line divide-by-zero destination may vary;
    // that path always executes the original body. Interior entries are untouched.
    static bool summarize_division_helper(translate_result &tr, const std::uint8_t *code, std::size_t size) {
        static constexpr std::uint32_t pattern[] = {
            0xe190c001u, 0x4a000021u, 0xe071c0a0u, 0xe3a02000u, 0x3a00001au, 0xe071c220u,
            0x3a00000fu, 0xe071c420u, 0x3a000001u, 0xe3a03000u, 0xea000020u, 0xe071c3a0u,
            0x20400381u, 0xe0a22002u, 0xe071c320u, 0x20400301u, 0xe0a22002u, 0xe071c2a0u,
            0x20400281u, 0xe0a22002u, 0xe071c220u, 0x20400201u, 0xe0a22002u, 0xe071c1a0u,
            0x20400181u, 0xe0a22002u, 0xe071c120u, 0x20400101u, 0xe0b22002u, 0xe071c0a0u,
            0x20400081u, 0xe0a22002u, 0xe0501001u, 0x31a01000u, 0xe0a20002u, 0xe12fff1eu,
            0xe2112102u, 0x42611000u, 0xe0323040u, 0x22600000u, 0xe071c220u, 0x3a00001du,
            0xe071c420u, 0x3a00000fu, 0xe1a01301u, 0xe071c420u, 0xe382233fu, 0x3a00000bu,
            0xe1a01301u, 0xe071c420u, 0xe382263fu, 0x3a000007u, 0xe1a01301u, 0xe071c420u,
            0xe382293fu, 0x23822c3fu, 0x21a01301u, 0xe271c000u, 0x2a00012bu, 0x21a01321u,
            0xe071c3a0u, 0x20400381u, 0xe0a22002u, 0xe071c320u, 0x20400301u, 0xe0a22002u,
            0xe071c2a0u, 0x20400281u, 0xe0a22002u, 0xe071c220u, 0x20400201u, 0xe0a22002u,
            0xe071c1a0u, 0x20400181u, 0xe0a22002u, 0xe071c120u, 0x20400101u, 0xe0b22002u,
            0x2affffebu, 0xe071c0a0u, 0x20400081u, 0xe0a22002u, 0xe0501001u, 0x31a01000u,
            0xe0a20002u, 0xe1b03fc3u, 0x42600000u, 0x22611000u, 0xe12fff1eu,
        };
        if (size < sizeof(pattern)) return false;
        for (unsigned i = 0; i < sizeof(pattern) / 4; ++i) {
            std::uint32_t actual; std::memcpy(&actual, code + i * 4, 4);
            const auto mask = i == 58 ? 0xff000000u : 0xffffffffu;
            if ((actual & mask) != (pattern[i] & mask)) return false;
        }
        std::vector<std::uint8_t> prefix;
        arm_emit w{prefix};
        const unsigned first = tr.func.num_prefix_i64_locals + tr.func.num_locals + 1;
        const unsigned ns = first, ds = first+1, n = first+2, d = first+3,
            q = first+4, rem = first+5, count = first+6, sign = first+7, tmp = first+8;
        tr.func.num_locals += 9;
        w.op(op_block); w.op(type_void);
        auto fallback = [&] { w.op(op_br_if); leb(prefix, 0); };
        if (!watchdog::enabled) { w.load_i32(S::AOT_BUDGET); w.i32_const(63); w.op(op_i32_lt_u); fallback(); }
        w.load_i32(S::AOT_EXIT); fallback();
        w.load_i32(S::NIRQ); w.op(op_i32_eqz);
        w.load_i32(S::CPSR); w.i32_const(0x80); w.op(op_i32_and); w.op(op_i32_eqz);
        w.op(op_i32_and); fallback();
        auto magnitude = [&](unsigned reg, unsigned value, unsigned negative) {
            w.load_reg(reg); w.tee_local(value); w.i32_const(31); w.op(op_i32_shr_s);
            w.tee_local(negative); w.get_local(value); w.op(op_i32_xor);
            w.get_local(negative); w.op(op_i32_sub); w.set_local(value);
        };
        magnitude(0, n, ns); magnitude(1, d, ds);
        w.get_local(d); w.op(op_i32_eqz); fallback();
        w.get_local(n); w.i32_const(8); w.op(op_i32_shr_u);
        w.get_local(d); w.op(op_i32_lt_u); fallback();
        // This path has quotient magnitude >= 256. Its full guest count depends
        // only on three quotient-size boundaries and the signed-entry path.
        // No digit loop or per-instruction accounting survives in the summary.
        if (!watchdog::enabled) {
            w.i32_const(64);
            for (auto threshold : {std::pair<unsigned,unsigned>{14,24}, {20,28}, {26,20}}) {
                w.get_local(n); w.i32_const(threshold.first); w.op(op_i32_shr_u);
                w.get_local(d); w.op(op_i32_ge_u); w.i32_const(threshold.second);
                w.op(op_i32_mul); w.op(op_i32_add);
            }
            w.get_local(ns); w.get_local(ds); w.op(op_i32_or); w.i32_const(1); w.op(op_i32_and);
            w.op(op_i32_sub); w.set_local(count);
            w.load_i32(S::AOT_BUDGET); w.get_local(count); w.op(op_i32_lt_u); fallback();
        }
        w.get_local(n); w.get_local(d); w.op(op_i32_div_u); w.set_local(q);
        w.get_local(n); w.get_local(q); w.get_local(d); w.op(op_i32_mul);
        w.op(op_i32_sub); w.set_local(rem);
        w.get_local(ns); w.get_local(ds); w.op(op_i32_xor); w.set_local(sign);
        auto signed_result = [&](unsigned value, unsigned negative, unsigned reg) {
            w.get_local(value); w.get_local(negative); w.op(op_i32_xor);
            w.get_local(negative); w.op(op_i32_sub); w.store_i32_from_stack(S::reg(reg), tmp);
        };
        signed_result(q, sign, 0); signed_result(rem, ns, 1);
        // Only these scratch outputs are modified. Unrelated registers pass
        // through untouched; packed status was read only for the IRQ guard.
        w.get_local(q); w.i32_const(1); w.op(op_i32_shr_u); w.store_i32_from_stack(S::reg(2), tmp);
        w.store_reg(3, sign);
        w.get_local(rem); w.get_local(q); w.i32_const(3); w.op(op_i32_and);
        w.get_local(d); w.op(op_i32_mul); w.op(op_i32_add); w.i32_const(1); w.op(op_i32_shr_u);
        w.get_local(d); w.op(op_i32_sub); w.store_i32_from_stack(S::reg(12), tmp);
        w.get_local(sign); w.i32_const(1); w.op(op_i32_and); w.store_i32_from_stack(S::NFLAG, tmp);
        w.get_local(sign); w.op(op_i32_eqz); w.store_i32_from_stack(S::ZFLAG, tmp);
        w.get_local(ns); w.i32_const(1); w.op(op_i32_and); w.store_i32_from_stack(S::CFLAG, tmp);
        // With >=256 quotient, the final remainder subtraction cannot overflow.
        w.store_i32_const(S::VFLAG, 0);
        w.load_reg(14); w.set_local(tmp); w.store_reg(15, tmp);
        w.get_local(tmp); w.i32_const(1); w.op(op_i32_and); w.store_i32_from_stack(S::TFLAG, tmp);
        if (watchdog::enabled) w.i32_const(1); else w.get_local(count);
        w.ret(); w.op(op_end);
        const auto offset = static_cast<std::uint32_t>(prefix.size());
        if (tr.func.outlined_callee) tr.func.outlined_call_offset += offset;
        for (auto &call : tr.func.outlined_calls) call.call_offset += offset;
        for (auto &mark : tr.func.sources) mark.offset += offset;
        if (!tr.func.sources.empty()) tr.func.sources.insert(tr.func.sources.begin(), {0, 0, source_kind::helper});
        prefix.insert(prefix.end(), tr.func.body.begin(), tr.func.body.end());
        tr.func.body = std::move(prefix);
        tr.summarized_helpers = 1;
        return true;
    }

    translate_result translate_arm_block(
        const std::uint8_t *code, std::size_t code_size, std::uint32_t start_address,
        const sibling_map *siblings, const code_window *dll_code, bool bounded,
        bool stop_after_store, bool cache_registers, bool region,
        const leaf_resolver *leaves, bool defer_memory, arm_ir_policy ir_policy)
    {
        auto precise = translate_arm_block_impl(code, code_size, start_address, siblings,
            dll_code, bounded, stop_after_store, cache_registers, region, leaves,
            defer_memory, true, false, ir_policy);
        if (bounded && region && cache_registers && precise.complete && !exit_census::enabled
            && summarize_list_scan(precise, code, code_size, start_address)) return precise;
        if (bounded && region && cache_registers && precise.complete && !exit_census::enabled
            && summarize_division_helper(precise, code, code_size)) return precise;
        if (watchdog::enabled || !entry_budget_mode || !bounded || !region || !cache_registers
            || !precise.complete || !precise.dependencies.empty()
            || code_size > 0xffffffffu - start_address) return precise;
        const auto extent = precise.end_address - start_address;
        if (extent < 8 || extent > code_size) return precise;
        // Each guest instruction can execute at most once, including untaken
        // predicates. Callee inlining and backward/self branches are excluded.
        for (std::size_t i = 0; i + 3 < extent; i += 4) {
            std::uint32_t inst; std::memcpy(&inst, code + i, 4);
            if (((inst >> 25) & 7) != 5 || (inst & (1u << 24))) continue;
            if ((static_cast<std::int32_t>(inst << 8) >> 6) <= -8) return precise;
        }
        auto full = translate_arm_block_impl(code, code_size, start_address, siblings,
            dll_code, bounded, stop_after_store, cache_registers, region, nullptr,
            defer_memory, true, true, ir_policy);
        if (!full.complete || full.end_address != precise.end_address
            || !full.dependencies.empty()) return precise;
        // The short-budget body has no private callees: generated modules keep
        // one outlining level even when the full path has a memory fallback.
        auto cold = translate_arm_block_impl(code, code_size, start_address, siblings,
            dll_code, bounded, stop_after_store, cache_registers, region, nullptr,
            defer_memory, false, false, ir_policy);
        if (cold.func.outlined_callee || !cold.func.outlined_calls.empty()
            || cold.end_address != precise.end_address || !cold.complete) return precise;
        std::vector<std::uint8_t> body;
        arm_emit w{body};
        w.load_i32(S::AOT_BUDGET); w.i32_const(extent / 4);
        std::uint32_t full_offset = 0, cold_call_offset = 0;
        w.op(op_i32_lt_u); w.op(op_if); w.op(type_void);
        w.state_ptr(); w.op(op_call);
        cold_call_offset = static_cast<std::uint32_t>(body.size());
        body.insert(body.end(), {0x80, 0x80, 0x80, 0x80, 0});
        w.op(op_return); w.op(op_end);
        full_offset = static_cast<std::uint32_t>(body.size());
        for (auto &mark : full.func.sources) mark.offset += full_offset;
        if (!full.func.sources.empty()) full.func.sources.insert(full.func.sources.begin(), {0, start_address, source_kind::accounting});
        body.insert(body.end(), full.func.body.begin(), full.func.body.end());
        if (full.func.outlined_callee) full.func.outlined_call_offset += full_offset;
        for (auto &call : full.func.outlined_calls) call.call_offset += full_offset;
        if (cold_call_offset) {
            cold.func.export_name += "_entry_budget_short";
            full.func.outlined_calls.push_back({std::make_shared<wasm_func_def>(std::move(cold.func)), cold_call_offset});
        }
        full.func.body = std::move(body);
        precise.func = std::move(full.func);
        precise.proved_reads = full.proved_reads;
        precise.proved_writes = full.proved_writes;
        precise.bail_count += full.bail_count;
        precise.entry_budget_instructions = extent / 4;
        return precise;
    }
}
