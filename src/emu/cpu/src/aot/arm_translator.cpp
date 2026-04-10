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

#include <cpu/aot/arm_translator.h>

#include <cstring>
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

    struct emit {
        std::vector<std::uint8_t> &b;
        bool unsupported = false;
        std::uint32_t bail_count = 0;

        void op(std::uint8_t o) { b.push_back(o); }
        void state_ptr() { op(op_local_get); leb(b, 0); }
        void get_local(std::uint32_t i) { op(op_local_get); leb(b, i); }
        void set_local(std::uint32_t i) { op(op_local_set); leb(b, i); }
        void tee_local(std::uint32_t i) { op(op_local_tee); leb(b, i); }
        void i32_const(std::int32_t v) { op(op_i32_const); sleb(b, v); }

        void load_i32(std::uint32_t offset) {
            state_ptr();
            op(op_i32_load); leb(b, 2); leb(b, offset);
        }
        void store_i32(std::uint32_t offset, std::uint32_t local) {
            state_ptr();
            get_local(local);
            op(op_i32_store); leb(b, 2); leb(b, offset);
        }
        void store_i32_const(std::uint32_t offset, std::int32_t val) {
            state_ptr();
            i32_const(val);
            op(op_i32_store); leb(b, 2); leb(b, offset);
        }

        void load_reg(int r) { load_i32(S::reg(r)); }
        void store_reg(int r, std::uint32_t local) { store_i32(S::reg(r), local); }

        void call(std::uint32_t func_idx) { op(op_call); leb(b, func_idx); }
        void ret() { op(op_return); }

        void bail(std::uint32_t pc, std::uint32_t instr_count) {
            store_i32_const(S::PC, static_cast<std::int32_t>(pc));
            i32_const(static_cast<std::int32_t>(instr_count));
            ret();
            bail_count++;
        }

        void bail_preserve_pc(std::uint32_t instr_count) {
            i32_const(static_cast<std::int32_t>(instr_count));
            ret();
            bail_count++;
        }

        void bail_unsupported(std::uint32_t pc, std::uint32_t instr_count) {
            unsupported = true;
            bail(pc, instr_count);
        }
    };

    // Emit condition check. ARM condition code in bits [31:28].
    // Emits: if (cond) { ... } with the caller responsible for closing the block.
    // Returns true if a conditional block was opened (caller must emit op_end).
    // Returns false for AL/NV (unconditional).
    static bool emit_cond_check(emit &w, std::uint32_t cond,
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
    static void emit_shifter_operand(emit &w, std::uint32_t inst,
                                     std::uint32_t TMP1, std::uint32_t TMP2,
                                     std::uint32_t TMP_CARRY) {
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
                // PC is at the instruction address + 8 in ARM
                // But we don't know the address here — caller must handle
                // For now, load from state
                w.load_reg(15);
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
                // Register shift: Rs in bits [11:8]
                int rs = (inst >> 8) & 0xF;
                w.load_reg(rs);
                w.i32_const(0xFF);
                w.op(op_i32_and); // only low byte
                w.set_local(TMP2);
                switch (shift_type) {
                case 0: // LSL
                    w.get_local(TMP1);
                    w.get_local(TMP2);
                    w.op(op_i32_shl);
                    break;
                case 1: // LSR
                    w.get_local(TMP1);
                    w.get_local(TMP2);
                    w.op(op_i32_shr_u);
                    break;
                case 2: // ASR
                    w.get_local(TMP1);
                    w.get_local(TMP2);
                    w.op(op_i32_shr_s);
                    break;
                case 3: // ROR
                    w.get_local(TMP1);
                    w.get_local(TMP2);
                    w.op(op_i32_rotr);
                    break;
                }
            }
        }
    }

    // Emit N/Z flag update from result in local `res`.
    static void emit_nz_flags(emit &w, std::uint32_t res, std::uint32_t tmp) {
        w.get_local(res);
        w.i32_const(31);
        w.op(op_i32_shr_u);
        w.set_local(tmp);
        w.store_i32(S::NFLAG, tmp);
        w.get_local(res);
        w.op(op_i32_eqz);
        w.set_local(tmp);
        w.store_i32(S::ZFLAG, tmp);
    }

    // CFG walker for ARM code. Returns reachable 4-byte-aligned offsets.
    static std::set<std::size_t> find_reachable_offsets_arm(
        const std::uint8_t *code, std::size_t code_size)
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
                        // BL: function call, fall through
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
                    if (rd == 15 && cond >= 0xE) {
                        // Could be a return (MOV PC, LR) — terminate
                        break;
                    }
                }

                i += 4;
            }
        }
        return reachable;
    }

    translate_result translate_arm_block(
        const std::uint8_t *code,
        std::size_t code_size,
        std::uint32_t start_address,
        const sibling_map *siblings,
        const code_window *dll_code)
    {
        translate_result tr;
        tr.complete = false;
        wasm_func_def &result = tr.func;
        result.export_name = "f_" + std::to_string(start_address);

        // Locals: 0=state_ptr(param), 1=tmp1, 2=tmp2, 3=tmp3, 4=tmp4, 5=pc_idx, 6=addr_tmp
        result.num_locals = 6;
        result.num_f32_locals = 0;
        result.num_f64_locals = 0;
        const std::uint32_t TMP1 = 1, TMP2 = 2, TMP3 = 3, TMP4 = 4;
        const std::uint32_t PC_IDX = 5, ADDR_TMP = 6;
        // TMP4 doubles as carry_out for shifter operand
        const std::uint32_t TMP_CARRY = TMP4;

        emit w{result.body, false};

        // Build instruction address → index map
        std::uint32_t num_insns = 0;
        for (std::size_t i = 0; i + 3 < code_size; i += 4) {
            num_insns++;
        }

        auto reachable = find_reachable_offsets_arm(code, code_size);

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
                if (target > src && target >= start_address &&
                    target < start_address + code_size) {
                    forward_targets_set.insert(target);
                }
            }
        }

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

        std::vector<std::uint32_t> fwd_sorted(
            forward_targets_set.begin(), forward_targets_set.end());
        std::unordered_map<std::uint32_t, std::uint32_t> fwd_idx;
        for (std::uint32_t k = 0; k < fwd_sorted.size(); k++) {
            fwd_idx[fwd_sorted[k]] = k;
        }

        // Initialize pc_idx = 0
        w.i32_const(0);
        w.set_local(PC_IDX);

        // block $exit
        w.op(op_block); w.op(type_void);
        // loop $loop
        w.op(op_loop); w.op(type_void);
        // Forward target blocks
        for (std::size_t k = 0; k < fwd_sorted.size(); k++) {
            w.op(op_block); w.op(type_void);
        }
        std::uint32_t closed_count = 0;
        const std::uint32_t N_fwd = static_cast<std::uint32_t>(fwd_sorted.size());

        std::uint32_t insn_idx = 0;
        std::uint32_t decoded_end_offset = 0;

        for (std::size_t i = 0; i + 3 < code_size; i += 4) {
            if (!reachable.count(i)) {
                insn_idx++;
                continue;
            }

            std::uint32_t inst;
            std::memcpy(&inst, code + i, 4);
            std::uint32_t insn_addr = start_address + static_cast<std::uint32_t>(i);

            // Close forward-target blocks
            while (closed_count < N_fwd && fwd_sorted[closed_count] == insn_addr) {
                w.op(op_end);
                closed_count++;
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
                            if (cond_opened) w.op(op_end);
                            w.ret();
                            tr.resume_points.push_back(next_pc);
                            insn_idx++;
                            decoded_end_offset = static_cast<std::uint32_t>(i) + 4;
                            continue;
                        }
                    }
                    w.store_i32_const(S::LR, static_cast<std::int32_t>(next_pc));
                    if (cond_opened) w.op(op_end);
                    w.bail(target, insn_idx + 1);
                    tr.resume_points.push_back(next_pc);
                    insn_idx++;
                    decoded_end_offset = static_cast<std::uint32_t>(i) + 4;
                    continue;
                }

                // B (not link)
                // Check if target is a forward branch within the block
                auto fit = fwd_idx.find(target);
                if (fit != fwd_idx.end()) {
                    // Forward branch: br to the appropriate block depth
                    std::uint32_t depth = fit->second - closed_count;
                    w.op(op_br);
                    leb(result.body, depth);
                    if (cond_opened) w.op(op_end);
                } else if (target >= start_address && target < start_address + code_size) {
                    // Backward branch within block: br to loop
                    std::uint32_t loop_depth = N_fwd - closed_count + 0; // loop is right after blocks
                    w.op(op_br);
                    leb(result.body, loop_depth);
                    if (cond_opened) w.op(op_end);
                } else {
                    // Out of block
                    if (cond_opened) w.op(op_end);
                    w.bail(target, insn_idx + 1);
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
                    if (cond_opened) w.op(op_end);
                    w.bail_preserve_pc(insn_idx + 1);
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
                    if (cond_opened) w.op(op_end);
                    w.bail_preserve_pc(insn_idx + 1);
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
                if (cond_opened) w.op(op_end);
                w.bail_preserve_pc(insn_idx + 1);
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

                int off = 0;
                for (int r = 0; r < 16; r++) {
                    if (!(reglist & (1 << r))) continue;
                    if (load) {
                        w.state_ptr();
                        w.get_local(TMP1);
                        if (off > 0) { w.i32_const(off); w.op(op_i32_add); }
                        w.call(0); // tlb_read32
                        w.set_local(TMP2);
                        w.store_reg(r, TMP2);
                    } else {
                        w.load_reg(r);
                        w.set_local(TMP2);
                        w.state_ptr();
                        w.get_local(TMP1);
                        if (off > 0) { w.i32_const(off); w.op(op_i32_add); }
                        w.get_local(TMP2);
                        w.call(1); // tlb_write32
                    }
                    off += 4;
                }

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
                    if (cond_opened) w.op(op_end);
                    w.bail_preserve_pc(insn_idx + 1);
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

                // MRS/MSR: bits [27:23] = 00010, I=0, bit 20 depends
                bool is_mrs_msr = ((inst >> 23) & 0x1F) == 2 &&
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
                    // UMULL/SMULL/UMLAL/SMLAL — bail for now (need i64)
                    if (cond_opened) w.op(op_end);
                    w.bail_unsupported(insn_addr, insn_idx);
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
                        // Post-indexed: use Rn as address, then compute Rn+offset
                        // For simplicity, bail on post-indexed for now
                        if (cond_opened) w.op(op_end);
                        w.bail_unsupported(insn_addr, insn_idx);
                        insn_idx++;
                        decoded_end_offset = static_cast<std::uint32_t>(i) + 4;
                        continue;
                    }

                    w.set_local(ADDR_TMP);

                    if (load) {
                        w.state_ptr();
                        w.get_local(ADDR_TMP);
                        if (sh == 1) {
                            // LDRH: unsigned halfword
                            w.call(0); // tlb_read32
                            w.i32_const(0xFFFF);
                            w.op(op_i32_and);
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
                            w.call(0); // tlb_read32
                            w.i32_const(0xFFFF);
                            w.op(op_i32_and);
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
                        w.call(1); // tlb_write32
                    }

                    if (writeback || !preindex) {
                        // Write-back: Rn = computed address
                        w.store_reg(rn, ADDR_TMP);
                    }

                    if (cond_opened) w.op(op_end);
                    insn_idx++;
                    decoded_end_offset = static_cast<std::uint32_t>(i) + 4;
                    continue;
                }

                if (is_mrs_msr) {
                    // Bail on MRS/MSR
                    if (cond_opened) w.op(op_end);
                    w.bail_unsupported(insn_addr, insn_idx);
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

                // For Rn==15, the value is PC+8 in ARM mode
                // We need to handle this specially for immediate ops
                bool rn_is_pc = (rn == 15);

                // Get Rn value
                if (rn_is_pc) {
                    w.i32_const(static_cast<std::int32_t>(insn_addr + 8));
                } else {
                    w.load_reg(rn);
                }
                w.set_local(TMP1); // Rn value

                // Get shifter operand
                emit_shifter_operand(w, inst, TMP2, TMP3, TMP_CARRY);
                w.set_local(TMP2); // operand 2

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
                        w.op(op_i32_ge_u); w.set_local(TMP4);
                        w.store_i32(S::CFLAG, TMP4);
                        // V = (Rn ^ op2) & (Rn ^ result) >> 31
                        w.get_local(TMP1); w.get_local(TMP2); w.op(op_i32_xor);
                        w.get_local(TMP1); w.get_local(TMP3); w.op(op_i32_xor);
                        w.op(op_i32_and); w.i32_const(31); w.op(op_i32_shr_u);
                        w.set_local(TMP4); w.store_i32(S::VFLAG, TMP4);
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
                        w.op(op_i32_ge_u); w.set_local(TMP4);
                        w.store_i32(S::CFLAG, TMP4);
                        // V = (op2 ^ Rn) & (op2 ^ result) >> 31
                        w.get_local(TMP2); w.get_local(TMP1); w.op(op_i32_xor);
                        w.get_local(TMP2); w.get_local(TMP3); w.op(op_i32_xor);
                        w.op(op_i32_and); w.i32_const(31); w.op(op_i32_shr_u);
                        w.set_local(TMP4); w.store_i32(S::VFLAG, TMP4);
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
                        w.op(op_i32_lt_u); w.set_local(TMP4);
                        w.store_i32(S::CFLAG, TMP4);
                        // V = ~(Rn ^ op2) & (Rn ^ result) >> 31
                        w.get_local(TMP1); w.get_local(TMP2); w.op(op_i32_xor);
                        w.i32_const(-1); w.op(op_i32_xor);
                        w.get_local(TMP1); w.get_local(TMP3); w.op(op_i32_xor);
                        w.op(op_i32_and); w.i32_const(31); w.op(op_i32_shr_u);
                        w.set_local(TMP4); w.store_i32(S::VFLAG, TMP4);
                    }
                    break;
                case 0x5: // ADC
                    w.get_local(TMP1); w.get_local(TMP2);
                    w.op(op_i32_add);
                    w.load_i32(S::CFLAG);
                    w.op(op_i32_add);
                    w.set_local(TMP3);
                    if (rd != 15) w.store_reg(rd, TMP3);
                    if (set_flags) emit_nz_flags(w, TMP3, TMP4);
                    break;
                case 0x6: // SBC
                    w.get_local(TMP1); w.get_local(TMP2);
                    w.op(op_i32_sub);
                    w.load_i32(S::CFLAG);
                    w.op(op_i32_eqz); // NOT C
                    w.op(op_i32_sub);
                    w.set_local(TMP3);
                    if (rd != 15) w.store_reg(rd, TMP3);
                    if (set_flags) emit_nz_flags(w, TMP3, TMP4);
                    break;
                case 0x7: // RSC
                    w.get_local(TMP2); w.get_local(TMP1);
                    w.op(op_i32_sub);
                    w.load_i32(S::CFLAG);
                    w.op(op_i32_eqz);
                    w.op(op_i32_sub);
                    w.set_local(TMP3);
                    if (rd != 15) w.store_reg(rd, TMP3);
                    if (set_flags) emit_nz_flags(w, TMP3, TMP4);
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
                    w.op(op_i32_ge_u); w.set_local(TMP4);
                    w.store_i32(S::CFLAG, TMP4);
                    // V
                    w.get_local(TMP1); w.get_local(TMP2); w.op(op_i32_xor);
                    w.get_local(TMP1); w.get_local(TMP3); w.op(op_i32_xor);
                    w.op(op_i32_and); w.i32_const(31); w.op(op_i32_shr_u);
                    w.set_local(TMP4); w.store_i32(S::VFLAG, TMP4);
                    break;
                case 0xB: // CMN (ADD without write)
                    w.get_local(TMP1); w.get_local(TMP2);
                    w.op(op_i32_add);
                    w.set_local(TMP3);
                    emit_nz_flags(w, TMP3, TMP4);
                    // C = result < Rn
                    w.get_local(TMP3); w.get_local(TMP1);
                    w.op(op_i32_lt_u); w.set_local(TMP4);
                    w.store_i32(S::CFLAG, TMP4);
                    // V = ~(Rn ^ op2) & (Rn ^ result) >> 31
                    w.get_local(TMP1); w.get_local(TMP2); w.op(op_i32_xor);
                    w.i32_const(-1); w.op(op_i32_xor);
                    w.get_local(TMP1); w.get_local(TMP3); w.op(op_i32_xor);
                    w.op(op_i32_and); w.i32_const(31); w.op(op_i32_shr_u);
                    w.set_local(TMP4); w.store_i32(S::VFLAG, TMP4);
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
                    // Check T flag from bit 0
                    w.get_local(TMP3);
                    w.i32_const(1);
                    w.op(op_i32_and);
                    w.set_local(TMP4);
                    w.store_i32(S::TFLAG, TMP4);
                    if (cond_opened) w.op(op_end);
                    w.bail_preserve_pc(insn_idx + 1);
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
                    // Post-indexed: use Rn as address, then update
                    // Bail for simplicity
                    if (cond_opened) w.op(op_end);
                    w.bail_unsupported(insn_addr, insn_idx);
                    insn_idx++;
                    decoded_end_offset = static_cast<std::uint32_t>(i) + 4;
                    continue;
                }

                w.set_local(ADDR_TMP);

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
                        if (cond_opened) w.op(op_end);
                        w.bail_preserve_pc(insn_idx + 1);
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
            if (cond_opened) w.op(op_end);

            static std::set<std::uint32_t> seen_arm;
            if (seen_arm.insert(inst & 0x0FFFFFFF).second) {
                fprintf(stderr, "AOT: unsupported ARM insn 0x%08X at 0x%08X\n", inst, insn_addr);
            }
            w.bail_unsupported(insn_addr, insn_idx);
            insn_idx++;
            decoded_end_offset = static_cast<std::uint32_t>(i) + 4;
            // Don't break — continue for subsequent instructions that might
            // be reachable via forward branches.
        }

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
        w.i32_const(num_insns);
        w.ret();

        tr.complete = !w.unsupported;
        tr.end_address = start_address + decoded_end_offset;
        tr.bail_count = w.bail_count;
        return tr;
    }
}
